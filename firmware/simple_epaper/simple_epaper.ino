// ESP32-S3 N16R8 with the Waveshare 7.3-inch six-color HAT (E).
#include <Arduino.h>
#include <SPI.h>
#include <GxEPD2_7C.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_heap_caps.h>
#include <esp_psram.h>
#include <esp_random.h>
#include <mbedtls/sha256.h>
#include <atomic>
#include <memory>
#include "frame_rules.h"
#include "panel_ota.h"
#include "release.h"
#if !defined(CONFIG_IDF_TARGET_ESP32S3)
#error "Select ESP32-S3 with 16 MB flash and 8 MB OPI PSRAM"
#endif
#if __has_include("family_page_gzip.h")
#include "family_page_gzip.h"
#else
#include "family_page.h"
#endif
#include "gallery_flash.h"
#include "album_order.h"

constexpr char Firmware[] = EPAPER_RELEASE;
constexpr int MOSI_PIN = 4, SCK_PIN = 5, CS_PIN = 6;
constexpr int DC_PIN = 7, RESET_PIN = 15, BUSY_PIN = 16;
constexpr uint32_t IntervalMs = 180000, BusyTimeoutMs = 120000;
constexpr uint32_t UploadBudgetMs = 45000;
constexpr size_t MaxSlides = 12;
constexpr size_t ThumbWidth = 80, ThumbHeight = 48, ThumbBytes = ThumbWidth * ThumbHeight / 2;
constexpr size_t GalleryWidth = 160, GalleryHeight = 96, GalleryBytes = GalleryWidth * GalleryHeight * 3;
struct DisplayedThumb { char sha[65]; uint8_t data[ThumbBytes]; };
struct NetConfig { char ssid[33]; char password[64]; char token[49]; };
struct LineBuffer { char data[320] = {}; size_t size = 0; bool overflow = false; };
struct RenderResult { const char *failure; uint32_t duration; };
class Panel : public GxEPD2_730c_GDEP073E01 {
 public:
  Panel() : GxEPD2_730c_GDEP073E01(CS_PIN, DC_PIN, RESET_PIN, BUSY_PIN) {
    _busy_timeout = BusyTimeoutMs * 1000;
  }
};
Panel panel;
SPIClass panelSpi(FSPI);
WebServer server(28463);
NetConfig net = {};
uint8_t *pixels = nullptr;
uint8_t *staging = nullptr;
uint8_t *album = nullptr;
uint8_t *albumStage = nullptr;
uint8_t *stagedFrame = nullptr, *shownFrame = nullptr;
bool shownFull = false, storageError = false;
GalleryFlash galleryFlash;
DisplayedThumb shown = {};
uint8_t stagedThumb[ThumbBytes] = {};
int shownSlot = -1, scheduledSlot = -1;
bool shownThisBoot = false;
uint8_t replaceSlots[2] = {};
String mac, hostname;
const char *state = "idle", *failure = "";
char digest[65] = {};
uint32_t refreshMs = 0, lastFinished = 0, lastConnect = 0;
bool sawBusy = false, spiStarted = false, serving = false, mdnsReady = false;
RenderResult renderResult;
std::atomic<bool> renderReady{false};
int uploadError = 0;
uint32_t uploadStarted = 0, jobSeq = 0, batchSeq = 0;
size_t uploadSize = 0, slideCount = 0, nextSlide = 0;
uint32_t slideInterval = 0, lastSlide = 0;
bool sourceCarousel = false, firstSlide = true, carouselPaused = false;
char albumHash[65] = {}, bootId[17] = {}, webJobId[17] = {}, batchRequestId[17] = {};

bool pending() { return !strcmp(state, "accepted") || !strcmp(state, "refreshing"); }
bool authorized() {
  const String value = server.header("Authorization");
  return net.token[0] && value.length() == 55 && value.startsWith("Bearer ") &&
    memcmp(value.c_str() + 7, net.token, 48) == 0;
}
bool supportedMemory() {
  return ESP.getFlashChipSize() == 16 * 1024 * 1024 && esp_psram_get_size() == 8 * 1024 * 1024;
}
String statusJson() {
  char out[600];
  uint32_t elapsed = millis() - lastFinished;
  snprintf(out, sizeof(out),
    "{\"firmware\":\"%s\",\"mac\":\"%s\",\"host\":\"%s.local\",\"ip\":\"%s\","
    "\"configured\":%s,\"connected\":%s,\"hardware_ready\":%s,\"storage_ready\":%s,\"slides\":%u,\"psram_bytes\":%u,\"frame_buffer\":%s,"
    "\"state\":\"%s\",\"sha256\":\"%s\",\"refresh_ms\":%u,\"wait_ms\":%u,\"failure\":\"%s\"}",
    Firmware, mac.c_str(), hostname.c_str(), WiFi.localIP().toString().c_str(),
    net.ssid[0] ? "true" : "false", WiFi.status() == WL_CONNECTED ? "true" : "false",
    supportedMemory() ? "true" : "false", galleryFlash.ready() ? "true" : "false", (unsigned)slideCount,
    (unsigned)ESP.getPsramSize(), pixels ? "true" : "false", state, digest, (unsigned)refreshMs,
    (unsigned)(elapsed < IntervalMs ? IntervalMs - elapsed : 0), failure);
  return String(out);
}
void sendStatus(int code) { server.send(code, "application/json", statusJson()); }
void reject(int code, const char *reason) {
  server.send(code, "application/json", String("{\"state\":\"rejected\",\"failure\":\"") + reason + "\"}");
}

bool hashHex(const uint8_t *data, size_t length, char (&hex)[65]) {
  uint8_t bytes[32];
  if (mbedtls_sha256(data, length, bytes, 0) != 0) return false;
  for (size_t i=0; i<sizeof(bytes); ++i) snprintf(hex + 2*i, 3, "%02x", bytes[i]);
  return true;
}
bool familyLanAllowed(bool write) {
  const IPAddress local = WiFi.localIP(), mask = WiFi.subnetMask(), peer = server.client().remoteIP();
  if (WiFi.status() != WL_CONNECTED || uint32_t(mask) == 0 || uint32_t(peer) == 0) return false;
  for (size_t i=0; i<4; ++i) {
    if ((local[i] & mask[i]) != (peer[i] & mask[i])) return false;
  }
  const String host = server.hostHeader();
  if (host != hostname + ".local:28463" && host != local.toString() + ":28463") return false;
  const String origin = server.header("Origin");
  if (!write && !origin.length()) return true;
  return origin == String("http://") + host;
}
bool carouselActive() { return slideCount != 0; }
String albumRevision() { return String(bootId) + "-" + String(batchSeq); }
void stopCarousel() {
  if (slideCount) { memset(album, 0, slideCount * FrameBytes); ++batchSeq; }
  slideCount = 0;
  carouselPaused = false;
  nextSlide = 0;
  shownSlot = -1;
  albumHash[0] = 0;
  batchRequestId[0] = 0;
}
bool clearCarousel() {
  if (!carouselActive()) return true;
  if (!galleryFlash.saveAlbum(nullptr,0,0,0)) { storageError = true; return false; }
  stopCarousel();
  storageError = false;
  return true;
}
void makeThumb(const uint8_t *frame, uint8_t *result) {
  for (size_t y=0; y<ThumbHeight; ++y) {
    for (size_t x=0; x<ThumbWidth; x+=2) {
      const size_t pixel = (y*10+5)*800 + x*10+5;
      result[(y*ThumbWidth+x)/2] = ((frame[pixel/2] & 15) << 4) | (frame[(pixel+10)/2] & 15);
    }
  }
}
void rememberShown() {
  memcpy(shown.sha, digest, sizeof(shown.sha));
  memcpy(shown.data, stagedThumb, ThumbBytes);
  shownSlot = scheduledSlot;
  shownThisBoot = true;
  if (stagedFrame && shownFrame) {
    uint8_t *temp = shownFrame;
    shownFrame = stagedFrame;
    stagedFrame = temp;
    shownFull = true;
    if (galleryFlash.ready()) {
      SavedScreen record;
      record.albumSequence = galleryFlash.albumSequence();
      record.slot = shownSlot < 0 ? UINT32_MAX : static_cast<uint32_t>(shownSlot);
      record.next = nextSlide;
      memcpy(record.sha,shown.sha,sizeof(record.sha));
      storageError = !galleryFlash.saveScreen(shownFrame,record);
    }
  }
}
void galleryThumbnail(const uint8_t *frame, uint8_t *rgb) {
  constexpr uint8_t colors[6][3] = {{67,52,57},{200,197,182},{216,177,48},{161,60,34},{63,89,145},{88,93,73}};
  for (size_t y=0; y<GalleryHeight; ++y) {
    for (size_t x=0; x<GalleryWidth; ++x) {
      unsigned total[3] = {};
      for (size_t dy=0; dy<5; ++dy) for (size_t dx=0; dx<5; ++dx) {
        const size_t pixel = (y*5+dy)*800 + x*5+dx;
        const uint8_t value = frame[pixel/2];
        const uint8_t index = pixel%2 ? value&15 : value>>4;
        for (size_t c=0;c<3;++c) total[c] += colors[index<6 ? index : 0][c];
      }
      for (size_t c=0;c<3;++c) *rgb++ = (total[c]+12)/25;
    }
  }
}
void sendGalleryPreviews() {
  if (!familyLanAllowed(false)) { reject(403,"family_lan_only"); return; }
  if (server.arg("revision") != albumRevision()) { reject(409,"album_changed"); return; }
  server.sendHeader("Cache-Control", "no-store");
  std::unique_ptr<uint8_t, decltype(&free)> thumb(
    slideCount ? (uint8_t *)heap_caps_malloc(GalleryBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : nullptr, &free);
  if (slideCount && !thumb) { reject(503,"preview_buffer_unavailable"); return; }
  server.setContentLength(slideCount * GalleryBytes);
  server.send(200, "application/octet-stream", "");
  for (size_t slot=0; slot<slideCount && server.client().connected(); ++slot) {
    galleryThumbnail(album + slot*FrameBytes, thumb.get());
    server.sendContent(reinterpret_cast<const char *>(thumb.get()), GalleryBytes);
  }
}
void sendShownPreview() {
  if (!familyLanAllowed(false)) { reject(403,"family_lan_only"); return; }
  if (!shown.sha[0] || server.arg("sha") != shown.sha) { reject(404,"preview_unavailable"); return; }
  server.sendHeader("Cache-Control", "no-store");
  const size_t length = shownFull ? FrameBytes : ThumbBytes;
  server.setContentLength(length);
  server.send(200, "application/octet-stream", "");
  server.sendContent(reinterpret_cast<const char *>(shownFull ? shownFrame : shown.data), length);
}
uint32_t remaining(uint32_t elapsed, uint32_t duration) {
  return elapsed < duration ? duration-elapsed : 0;
}
void sendWebStatus(int code) {
  char out[840];
  const uint32_t now = millis();
  const uint32_t cooldown = remaining(now-lastFinished, IntervalMs);
  const uint32_t rotationWait = firstSlide ? cooldown : max(cooldown, remaining(now-lastSlide, slideInterval));
  int length = snprintf(out, sizeof(out),
    "{\"boot_id\":\"%s\",\"job\":%lu,\"web_job\":\"%s\",\"source\":\"%s\",\"state\":\"%s\",\"sha256\":\"%s\","
    "\"refresh_ms\":%lu,\"wait_ms\":%lu,\"failure\":\"%s\",\"batch\":%lu,\"batch_request\":\"%s\",\"album_sha\":\"%s\","
    "\"slides\":%u,\"interval_minutes\":%lu,\"next_slide\":%u,\"next_ms\":%lu,\"photo_ready\":%s,\"album_ready\":%s,"
    "\"paused\":%s,\"revision\":\"%s\",\"shown_sha\":\"%s\",\"shown_slot\":%d,\"shown_current\":%s,"
    "\"shown_size\":%u,\"storage_ready\":%s,\"storage_error\":%s}",
    bootId, (unsigned long)jobSeq, webJobId, sourceCarousel ? "carousel" : "manual", state, digest,
    (unsigned long)refreshMs, (unsigned long)cooldown, failure, (unsigned long)batchSeq, batchRequestId, albumHash,
    (unsigned)slideCount, (unsigned long)(slideCount ? slideInterval/60000UL : 0), (unsigned)nextSlide, (unsigned long)(carouselActive() ? rotationWait : 0),
    pixels && staging ? "true" : "false", pixels && album && albumStage && galleryFlash.ready() ? "true" : "false",
    carouselPaused ? "true" : "false", albumRevision().c_str(), shown.sha, shownSlot,
    shownThisBoot ? "true" : "false", (unsigned)(shownFull ? FrameBytes : shown.sha[0] ? ThumbBytes : 0),
    galleryFlash.ready() ? "true" : "false", storageError ? "true" : "false");
  if (length < 0 || (size_t)length >= sizeof(out)) { reject(500, "status_overflow"); return; }
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", out);
}
void scheduleFrame(const uint8_t *data, const char *sha, int slot) {
  makeThumb(data, stagedThumb);
  if (stagedFrame) memcpy(stagedFrame,data,FrameBytes);
  scheduledSlot = slot;
  memcpy(pixels, data, FrameBytes);
  nativeFrame(pixels, FrameBytes);
  memcpy(digest, sha, sizeof(digest));
  ++jobSeq;
  webJobId[0] = 0;
  sourceCarousel = slot >= 0;
  state = "accepted";
  failure = "";
  refreshMs = 0;
}
uint32_t requestedInterval() {
  const String value = server.header("X-Rotation-Minutes");
  if (value == "15") return 15 * 60000UL;
  if (value == "30") return 30 * 60000UL;
  if (value == "60") return 60 * 60000UL;
  return 0;
}
void arrangeAlbum() {
  if (!familyLanAllowed(true)) { reject(403,"family_lan_only"); return; }
  if (otaPending() || pending() || !carouselActive() || server.header("X-Album-Revision") != albumRevision()) {
    reject(409,"album_changed_or_busy"); return;
  }
  if (!albumStage || !galleryFlash.ready()) { reject(503,"storage_unavailable"); return; }
  const String request = server.header("X-Web-Request");
  const uint32_t interval = requestedInterval();
  AlbumOrder order;
  if (request.length()!=16 || !hexLower(request.c_str(),16) || !interval ||
      !order.parse(server.header("X-Album-Slots").c_str(),slideCount)) { reject(400,"invalid_order"); return; }
  // A paused album retries its failed slot if the family keeps that photo.
  const size_t next = order.nextAfter(carouselPaused ? -1 : shownSlot,nextSlide,slideCount);
  for (size_t i=0;i<order.count;++i)
    memcpy(albumStage+i*FrameBytes,album+order.slots[i]*FrameBytes,FrameBytes);
  if (!galleryFlash.saveAlbum(albumStage,order.count,interval,next)) {
    storageError=true; reject(507,"album_save_failed"); return;
  }
  uint8_t *previous=album; album=albumStage; albumStage=previous;
  shownSlot=order.position(shownSlot);
  slideCount=order.count; nextSlide=next; slideInterval=interval; storageError=false;
  ++batchSeq; albumHash[0]=0; request.toCharArray(batchRequestId,sizeof(batchRequestId));
  sendWebStatus(200);
}
bool readReplaceSlots(size_t count) {
  const String value = server.header("X-Album-Slots");
  if (value.length() > 5 || !count || count > 2) return false;
  size_t cursor = 0;
  for (size_t i=0; i<count; ++i) {
    unsigned slot = 0;
    size_t digits = 0;
    while (cursor < value.length() && value[cursor] >= '0' && value[cursor] <= '9') {
      slot = slot*10 + value[cursor++]-'0';
      if (++digits > 2) return false;
    }
    if (!digits || slot >= slideCount || (i && slot <= replaceSlots[i-1])) return false;
    replaceSlots[i] = slot;
    if (i+1 < count && (cursor >= value.length() || value[cursor++] != ',')) return false;
  }
  return cursor == value.length();
}
void processRaw(bool web, bool batch, bool replace) {
  const auto &raw = server.raw();
  uint8_t *target = batch ? albumStage : staging;
  if (raw.status == RAW_START) {
    uploadError = 0;
    uploadStarted = millis();
    uploadSize = server.clientContentLength();
    String wanted = server.header("X-Frame-SHA256");
    const String requestId = web ? server.header("X-Web-Request") : String();
    const bool sizeOk = replace ? uploadSize == FrameBytes || uploadSize == 2*FrameBytes :
      batch ? uploadSize >= 2*FrameBytes && uploadSize <= MaxSlides*FrameBytes && uploadSize % FrameBytes == 0 && requestedInterval()
            : uploadSize == FrameBytes;
    const String revision = server.header("X-Album-Revision");
    if (web && !familyLanAllowed(true)) uploadError = 403;
    else if (!web && !authorized()) uploadError = 401;
    else if (!sizeOk || wanted.length() != 64 ||
      !hexLower(wanted.c_str(), 64) || (web && (requestId.length() != 16 || !hexLower(requestId.c_str(),16))) ||
      server.header("Content-Type") != "application/octet-stream") uploadError = 400;
    else if (!pixels || !target || (batch && (!album || !galleryFlash.ready()))) uploadError = 503;
    else if (web && !batch && (carouselActive() || revision.length()) && revision != albumRevision()) uploadError = 409;
    else if (otaPending() || !strcmp(state,"refreshing") || (batch && (pending() || ((replace || carouselActive() || revision.length()) && revision != albumRevision()))) ||
             (!batch && pending() && (wanted != digest || (web && requestId != webJobId)))) uploadError = 409;
    else if (replace && (!carouselActive() || !readReplaceSlots(uploadSize/FrameBytes))) uploadError = 400;
    if (uploadError) { reject(uploadError, "upload_not_accepted"); server.client().stop(); }
  } else if (raw.status == RAW_WRITE && !uploadError) {
    if (raw.totalSize > uploadSize || raw.currentSize > raw.totalSize || millis()-uploadStarted > UploadBudgetMs) {
      uploadError = 408;
      reject(uploadError, "upload_incomplete");
      server.client().stop();
    } else memcpy(target + raw.totalSize - raw.currentSize, raw.buf, raw.currentSize);
  } else if (raw.status == RAW_ABORTED) uploadError = 400;
}
void acceptFrame(bool web, bool batch, bool replace) {
  if (web && !familyLanAllowed(true)) { reject(403, "family_lan_only"); return; }
  if (!web && !authorized()) { reject(401, "unauthorized"); return; }
  if (server.header("Content-Type") != "application/octet-stream") { reject(400, "invalid_content_type"); return; }
  if (uploadError) return;
  if (server.raw().status != RAW_END || server.raw().totalSize != uploadSize) {
    reject(400, "incomplete_frame"); return;
  }
  const uint8_t *data = batch ? albumStage : staging;
  char actual[65];
  if (!hashHex(data, uploadSize, actual)) { reject(500, "hash_error"); return; }
  if (server.header("X-Frame-SHA256") != actual) { reject(400, "digest_mismatch"); return; }
  for (size_t offset=0; offset<uploadSize; offset+=FrameBytes) {
    if (!validFrame(data+offset, FrameBytes)) { reject(400, "invalid_palette"); return; }
  }
  if (batch) {
    size_t countAfter = uploadSize / FrameBytes;
    uint32_t intervalAfter = requestedInterval();
    size_t nextAfter = 0;
    bool firstAfter = true;
    int shownAfter = -1;
    if (replace) {
      const size_t replacements = countAfter;
      countAfter = slideCount;
      intervalAfter = slideInterval;
      nextAfter = nextSlide;
      firstAfter = firstSlide;
      shownAfter = shownSlot;
      // Sorted slots are at or above their staged indices; move backwards so
      // a destination never overwrites a replacement that still needs reading.
      for (size_t index=replacements; index>0; --index) {
        const int slot = replaceSlots[index-1];
        memmove(albumStage + slot*FrameBytes, albumStage + (index-1)*FrameBytes, FrameBytes);
        if (shownSlot == slot) { nextAfter = slot; firstAfter = true; shownAfter = -1; }
      }
      for (size_t slot=0;slot<slideCount;++slot) {
        if (slot != replaceSlots[0] && (replacements == 1 || slot != replaceSlots[1])) {
          memcpy(albumStage+slot*FrameBytes,album+slot*FrameBytes,FrameBytes);
        }
      }
    }
    if (!galleryFlash.saveAlbum(albumStage,countAfter,intervalAfter,nextAfter)) {
      storageError = true; reject(507,"album_save_failed"); return;
    }
    storageError = false;
    uint8_t *previous = album;
    album = albumStage;
    albumStage = previous;
    memset(albumStage, 0, slideCount*FrameBytes);
    slideInterval = intervalAfter;
    slideCount = countAfter;
    nextSlide = nextAfter;
    firstSlide = firstAfter;
    if (!replace) carouselPaused = false;
    shownSlot = shownAfter;
    ++batchSeq;
    memcpy(albumHash, actual, sizeof(albumHash));
    server.header("X-Web-Request").toCharArray(batchRequestId,sizeof(batchRequestId));
    sendWebStatus(202);
    return;
  }
  if (!clearCarousel()) { reject(507,"album_clear_failed"); return; }
  const bool duplicate = !strcmp(actual, digest) && strcmp(state, "failed");
  if (!duplicate) scheduleFrame(data, actual, -1);
  else if (web && !pending()) { ++jobSeq; sourceCarousel = false; }
  if (web) server.header("X-Web-Request").toCharArray(webJobId,sizeof(webJobId));
  const int code = !strcmp(state, "displayed") ? 200 : 202;
  if (web) sendWebStatus(code);
  else sendStatus(code);
}
void tickCarousel() {
  if (!carouselActive() || carouselPaused || pending() || otaPending()) return;
  const uint32_t now = millis();
  if (now-lastFinished < IntervalMs || (!firstSlide && now-lastSlide < slideInterval)) return;
  const uint8_t *data = album + nextSlide*FrameBytes;
  const int slot = nextSlide;
  char sha[65];
  if (!hashHex(data, FrameBytes, sha)) {
    carouselPaused = true; state = "failed"; failure = "album_hash_failed";
    return;
  }
  nextSlide = (nextSlide+1) % slideCount;
  firstSlide = false;
  lastSlide = now;
  if (strcmp(state, "displayed") || strcmp(digest, sha)) scheduleFrame(data, sha, slot);
  else shownSlot = slot;
}
void noteBusy(const void *) { sawBusy = true; delay(1); }
RenderResult renderFrame() {
  if (!spiStarted) {
    panelSpi.begin(SCK_PIN, -1, MOSI_PIN, CS_PIN);
    panel.selectSPI(panelSpi, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    panel.setBusyCallback(noteBusy);
    spiStarted = true;
  }
  // A sleeping HAT holds BUSY low. Reset BEFORE checking readiness.
  panel.init(0, true, 2, false);
  uint32_t started = millis();
  while (digitalRead(BUSY_PIN) == LOW && millis() - started < 1500) delay(2);
  if (digitalRead(BUSY_PIN) == LOW) return {"busy_after_reset",0};
  // invert=true selects the driver's already-native nibble path, not RGB inversion.
  panel.writeNative(pixels, nullptr, 0, 0, 800, 480, true, false, false);
  if (digitalRead(BUSY_PIN) == LOW) return {"power_on_timeout",0};
  sawBusy = false; started = millis();
  panel.refresh(false);
  const uint32_t duration = millis() - started;
  if (digitalRead(BUSY_PIN) == LOW) return {"refresh_timeout",duration};
  const bool confirmed = sawBusy && duration >= 10000 && duration < BusyTimeoutMs;
  panel.powerOff();
  if (digitalRead(BUSY_PIN) == LOW) return {"power_off_timeout",duration};
  panel.hibernate();
  return {confirmed ? "" : "refresh_not_observed",duration};
}
void renderOnWorker(void *) {
  renderResult = renderFrame();
  renderReady.store(true, std::memory_order_release);
  vTaskDelete(nullptr);
}
void finishRender(const RenderResult &result) {
  failure = result.failure;
  refreshMs = result.duration;
  state = *failure ? "failed" : "displayed";
  lastFinished = millis();
  if (*failure) {
    shownThisBoot = false;
    if (sourceCarousel && carouselActive()) {
      carouselPaused = true;
      if (scheduledSlot >= 0 && static_cast<size_t>(scheduledSlot) < slideCount) nextSlide = scheduledSlot;
    }
    return;
  }
  rememberShown();
  if (sourceCarousel) lastSlide = lastFinished;
}
bool validConfig(const NetConfig &value) {
  return value.ssid[0] && value.ssid[32] == 0 && value.password[63] == 0 &&
    strlen(value.password) >= 8 && value.token[48] == 0 && hexLower(value.token, 48);
}
void connectNetwork() {
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(hostname.c_str());
  WiFi.setAutoReconnect(true);
  WiFi.begin(net.ssid, net.password);
  lastConnect = millis();
}
bool configure(const char *hex, size_t size) {
  if (pending() || size % 2 || size < 2*58 || size > 2*145 || !hexLower(hex, size)) return false;
  uint8_t payload[145] = {};
  for (size_t i = 0; i < size/2; ++i) payload[i] = (hexDigit(hex[2*i]) << 4) | hexDigit(hex[2*i+1]);
  size_t a = payload[0], b = payload[1];
  if (!a || a > 32 || b < 8 || b > 63 || size/2 != 2+a+b+48) { memset(payload,0,sizeof(payload)); return false; }
  NetConfig next = {};
  memcpy(next.ssid, payload+2, a); memcpy(next.password, payload+2+a, b);
  memcpy(next.token, payload+2+a+b, 48);
  memset(payload,0,sizeof(payload));
  if (!validConfig(next)) { memset(&next,0,sizeof(next)); return false; }
  Preferences prefs;
  bool saved = prefs.begin("xiaoyi-frame") && prefs.putBytes("network", &next, sizeof(next)) == sizeof(next);
  prefs.end();
  if (saved) {
    if (serving) server.stop(); serving = false;
    if (mdnsReady) MDNS.end(); mdnsReady = false;
    WiFi.disconnect(); net = next; connectNetwork();
  }
  memset(&next,0,sizeof(next));
  return saved;
}
bool provisionOtaKey(const char *hex) {
  if (strlen(hex) != OtaKeyBytes * 2 || !hexLower(hex, OtaKeyBytes * 2) || otaPending()) return false;
  uint8_t key[OtaKeyBytes];
  for (size_t i = 0; i < sizeof(key); ++i) key[i] = (hexDigit(hex[2*i]) << 4) | hexDigit(hex[2*i+1]);
  Preferences prefs;
  bool ok = prefs.begin("xiaoyi-frame") && prefs.putBytes("ota-key", key, sizeof(key)) == sizeof(key);
  prefs.end();
  if (ok) setPanelOtaKey(key);
  memset(key, 0, sizeof(key));
  return ok;
}
LineBuffer usbLine, uartLine;
void serialCommands(Stream &io, LineBuffer &line) {
  while (io.available()) {
    char c = io.read();
    if (c == '\r') continue;
    if (c != '\n') {
      if (line.size < sizeof(line.data)-1) line.data[line.size++] = c;
      else line.overflow = true;
      continue;
    }
    line.data[line.size] = 0;
    if (!line.overflow && !strcmp(line.data, "EPD INFO")) io.println(statusJson());
    else if (!line.overflow && !strncmp(line.data, "EPD OTAKEY ", 11))
      io.println(provisionOtaKey(line.data+11) ? "{\"ota_key_saved\":true}" : "{\"ota_key_saved\":false}");
    else if (!line.overflow && !strncmp(line.data,"EPD CONFIG ",11))
      io.println(configure(line.data+11,line.size-11) ? "{\"configured\":true}" : "{\"configured\":false}");
    else io.println("{\"error\":\"unknown_or_oversized_command\"}");
    memset(line.data,0,sizeof(line.data)); line.size = 0; line.overflow = false;
  }
}
void setup() {
  Serial.begin(115200); Serial0.begin(115200);
  uint64_t id = ESP.getEfuseMac(); char macbuf[18];
  snprintf(macbuf,sizeof(macbuf),"%02x:%02x:%02x:%02x:%02x:%02x",(uint8_t)id,(uint8_t)(id>>8),
    (uint8_t)(id>>16),(uint8_t)(id>>24),(uint8_t)(id>>32),(uint8_t)(id>>40));
  mac = macbuf; hostname = "epaper-" + mac; hostname.replace(":", "");
  pixels = (uint8_t *)heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  staging = (uint8_t *)heap_caps_malloc(FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  album = (uint8_t *)heap_caps_malloc(MaxSlides*FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (album) albumStage = (uint8_t *)heap_caps_malloc(MaxSlides*FrameBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  stagedFrame = (uint8_t *)heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  shownFrame = (uint8_t *)heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  snprintf(bootId,sizeof(bootId),"%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random());
  if (!pixels) { state = "failed"; failure = "psram_unavailable"; }
  const char *headers[] = {"Authorization","Content-Type","X-Frame-SHA256","X-OTA-Nonce","X-OTA-Proof","X-App-SHA256",
                           "Origin","X-Rotation-Minutes","X-Web-Request","X-Album-Revision","X-Album-Slots"};
  server.collectHeaders(headers,sizeof(headers)/sizeof(headers[0]));
  initPanelOta(pixels && supportedMemory());
  registerPanelOta(server, [] { return !pending() && !carouselActive(); });
  server.on("/frame",HTTP_PUT,[]{acceptFrame(false,false,false);},[]{processRaw(false,false,false);});
  server.on("/status",HTTP_GET,[] { if (authorized()) sendStatus(200); else reject(401,"unauthorized"); });
  server.on("/",HTTP_GET,[] {
    if (!familyLanAllowed(false)) { reject(403,"family_lan_only"); return; }
    server.sendHeader("Cache-Control", "no-store");
    server.sendHeader("Content-Security-Policy", "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src blob:; worker-src blob:; connect-src 'self'; frame-ancestors 'none'");
#if __has_include("family_page_gzip.h")
    server.sendHeader("Content-Encoding", "gzip");
    server.send_P(200, "text/html; charset=utf-8", reinterpret_cast<const char *>(FamilyPageGzip), sizeof(FamilyPageGzip));
#else
    server.send_P(200, "text/html; charset=utf-8", FamilyPage);
#endif
  });
  server.on("/web/status",HTTP_GET,[] { if (familyLanAllowed(false)) sendWebStatus(200); else reject(403,"family_lan_only"); });
  server.on("/web/shown-preview",HTTP_GET,sendShownPreview);
  server.on("/web/album-previews",HTTP_GET,sendGalleryPreviews);
  server.on("/web/frame",HTTP_PUT,[]{acceptFrame(true,false,false);},[]{processRaw(true,false,false);});
  server.on("/web/carousel",HTTP_PUT,[]{acceptFrame(true,true,false);},[]{processRaw(true,true,false);});
  server.on("/web/replace",HTTP_PUT,[]{acceptFrame(true,true,true);},[]{processRaw(true,true,true);});
  server.on("/web/arrange",HTTP_POST,arrangeAlbum);
  server.on("/web/resume",HTTP_POST,[] {
    if (!familyLanAllowed(true)) { reject(403,"family_lan_only"); return; }
    if (otaPending() || !carouselActive() || server.header("X-Album-Revision") != albumRevision()) {
      reject(409,"album_changed"); return;
    }
    if (carouselPaused) { carouselPaused = false; firstSlide = true; }
    sendWebStatus(200);
  });
  server.on("/web/stop",HTTP_POST,[] {
    if (!familyLanAllowed(true)) { reject(403,"family_lan_only"); return; }
    if (pending() || otaPending()) { reject(409,"device_busy"); return; }
    if (server.header("X-Album-Revision") != albumRevision()) { reject(409,"album_changed"); return; }
    if (!clearCarousel()) { reject(507,"album_clear_failed"); return; }
    sendWebStatus(200);
  });
  Preferences prefs;
  if (prefs.begin("xiaoyi-frame", true)) {
    uint8_t key[OtaKeyBytes];
    if (prefs.getBytesLength("ota-key") == sizeof(key) && prefs.getBytes("ota-key", key, sizeof(key)) == sizeof(key)) setPanelOtaKey(key);
    memset(key, 0, sizeof(key));
    if (prefs.getBytesLength("network") == sizeof(net)) prefs.getBytes("network",&net,sizeof(net));
    if (prefs.getBytesLength("shown-thumb") == sizeof(shown)) prefs.getBytes("shown-thumb",&shown,sizeof(shown));
    prefs.end();
  }
  if (shown.sha[64] || !hexLower(shown.sha,64)) shown.sha[0] = 0;
  for (uint8_t color : shown.data) if ((color>>4)>5 || (color&15)>5) { shown.sha[0] = 0; break; }
  if (supportedMemory() && galleryFlash.begin()) {
    SavedAlbum playlist;
    if (pixels && album && albumStage && galleryFlash.readAlbum(album,playlist)) {
      slideCount = playlist.count;
      slideInterval = playlist.interval;
      nextSlide = playlist.next;
    }
    SavedScreen screen;
    if (shownFrame && stagedFrame && galleryFlash.readScreen(shownFrame,screen) && validFrame(shownFrame,FrameBytes)) {
      memcpy(shown.sha,screen.sha,sizeof(shown.sha));
      shownFull = true;
      if (slideCount && screen.albumSequence == playlist.sequence && screen.slot < slideCount && screen.next < slideCount) {
        nextSlide = screen.next;
        shownSlot = screen.slot;
      } else if (slideCount) {
        // An album edit can change slot numbers without changing the displayed pixels.
        const size_t previous = (nextSlide + slideCount - 1) % slideCount;
        for (size_t offset = 0; offset < slideCount; ++offset) {
          const size_t slot = (previous + offset) % slideCount;
          if (memcmp(album + slot * FrameBytes, shownFrame, FrameBytes) == 0) {
            shownSlot = static_cast<int>(slot);
            break;
          }
        }
      }
    }
  }
  if (validConfig(net)) connectNetwork(); else memset(&net,0,sizeof(net));
  lastFinished = millis();  // Every boot waits 180 s before the first requested refresh.
  Serial.println(statusJson()); Serial0.println(statusJson());
}
void loop() {
  if (renderReady.exchange(false, std::memory_order_acquire)) finishRender(renderResult);
  tickOta();
  serialCommands(Serial,usbLine); serialCommands(Serial0,uartLine);
  if (WiFi.status() == WL_CONNECTED) {
    if (!serving) { server.begin(); serving = true; }
    if (!mdnsReady) { mdnsReady = MDNS.begin(hostname.c_str()); if (mdnsReady) MDNS.addService("http","tcp",28463); }
    server.handleClient();
  } else if (net.ssid[0] && millis()-lastConnect > 30000) {
    WiFi.reconnect(); lastConnect = millis();
  }
  tickCarousel();
  if (!strcmp(state,"accepted") && millis()-lastFinished >= IntervalMs) {
    state = "refreshing";
    // The worker owns the panel and reads pixels. HTTP writes are gated until the result arrives.
    if (xTaskCreate(renderOnWorker, "epaper", 4096, nullptr, 1, nullptr) != pdPASS)
      finishRender({"render_worker_unavailable",0});
  }
  delay(2);
}
