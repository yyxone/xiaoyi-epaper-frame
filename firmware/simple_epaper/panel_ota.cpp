#include "panel_ota.h"
#include "release.h"
#include <Arduino.h>
#include <WebServer.h>
#include <atomic>
#include <esp_mac.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>

namespace {
constexpr char BoardId[] = "XIAOYI-FRAME-S3-N16R8";
constexpr char Version[] = EPAPER_RELEASE;
constexpr size_t SlotBytes = 0x300000;
constexpr uint32_t ChallengeMs = 20000;
constexpr uint32_t ConfirmMs = 75000;

WebServer *http = nullptr;
bool (*screenIdle)() = nullptr;
const esp_partition_t *running = nullptr;
const esp_partition_t *target = nullptr;
esp_ota_handle_t writer = 0;
mbedtls_sha256_context imageSha;
uint8_t authKey[OtaKeyBytes]{};
char device[18]{};
char nonce[33]{};
uint32_t issued = 0;
uint32_t bootAt = 0;
bool keySet = false;
bool hardwareOk = false;
bool receiving = false;
std::atomic<bool> pending{false};

void hex(const uint8_t *data, size_t size, char *out) {
  static constexpr char digits[] = "0123456789abcdef";
  for (size_t i = 0; i < size; ++i) {
    out[i * 2] = digits[data[i] >> 4];
    out[i * 2 + 1] = digits[data[i] & 15];
  }
  out[size * 2] = 0;
}

bool lowerHex(const String &value, size_t size) {
  if (value.length() != size) return false;
  for (char c : value) {
    if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f')) return false;
  }
  return true;
}

bool hmac(const String &message, char output[65]) {
  uint8_t digest[32];
  if (!keySet || mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), authKey,
                                sizeof(authKey), reinterpret_cast<const uint8_t *>(message.c_str()),
                                message.length(), digest) != 0) return false;
  hex(digest, sizeof(digest), output);
  return true;
}

bool equalProof(const String &actual, const char *expected) {
  if (!lowerHex(actual, 64)) return false;
  uint8_t mismatch = 0;
  for (size_t i = 0; i < 64; ++i) mismatch |= actual[i] ^ expected[i];
  return mismatch == 0;
}

bool authorized(const char *action, const String &detail) {
  if (!nonce[0] || millis() - issued > ChallengeMs || http->header("X-OTA-Nonce") != nonce) return false;
  String message = String("diy-ota-request-v1|") + device + "|" + Version + "|" + nonce + "|" + action + "|" + detail;
  char expected[65];
  if (!hmac(message, expected) || !equalProof(http->header("X-OTA-Proof"), expected)) return false;
  nonce[0] = 0;
  return true;
}

void reply(int code, const char *state) {
  http->send(code, "application/json", String("{\"state\":\"") + state + "\"}");
}

void sendInfo() {
  if (!keySet || !running) {
    reply(404, "not_provisioned");
    return;
  }
  uint8_t random[16];
  esp_fill_random(random, sizeof(random));
  hex(random, sizeof(random), nonce);
  issued = millis();
  bool verify = pending.load();
  String fields = String(device) + "|" + BoardId + "|" + Version + "|" + running->label +
                  "|" + (verify ? "1" : "0") + "|" + (hardwareOk ? "1" : "0") + "|" + nonce;
  char proof[65];
  if (!hmac(String("diy-ota-status-v1|") + fields, proof)) {
    reply(500, "proof_error");
    return;
  }
  char body[420];
  snprintf(body, sizeof(body),
           "{\"mac\":\"%s\",\"board\":\"%s\",\"version\":\"%s\",\"slot\":\"%s\","
           "\"pending\":%s,\"ready\":%s,\"nonce\":\"%s\",\"proof\":\"%s\"}",
           device, BoardId, Version, running->label, verify ? "true" : "false",
           hardwareOk ? "true" : "false", nonce, proof);
  http->send(200, "application/json", body);
}

void closeWriter() {
  if (receiving) {
    esp_ota_abort(writer);
    mbedtls_sha256_free(&imageSha);
    receiving = false;
  }
}

void rejectBody(int code, const char *state) {
  closeWriter();
  reply(code, state);
  http->client().stop();
}

void readImage() {
  auto &raw = http->raw();
  if (raw.status == RAW_START) {
    const String digest = http->header("X-App-SHA256");
    int size = http->clientContentLength();
    if (!authorized("upload", digest + "|" + String(size))) {
      rejectBody(401, "unauthorized");
      return;
    }
    if (!lowerHex(digest, 64) || size <= 0 || size > SlotBytes ||
        http->header("Content-Type") != "application/octet-stream") {
      rejectBody(400, "invalid_image_headers");
      return;
    }
    if (pending.load() || !hardwareOk || !screenIdle()) {
      rejectBody(409, "device_busy");
      return;
    }
    target = esp_ota_get_next_update_partition(running);
    if (!target || !running || target->address == running->address ||
        (target->address != 0x10000 && target->address != 0x310000) || target->size != SlotBytes) {
      rejectBody(409, "invalid_update_slot");
      return;
    }
    if (esp_ota_begin(target, size, &writer) != ESP_OK) {
      rejectBody(500, "cannot_begin");
      return;
    }
    receiving = true;
    mbedtls_sha256_init(&imageSha);
    if (mbedtls_sha256_starts(&imageSha, 0) != 0) rejectBody(500, "digest_error");
  } else if (raw.status == RAW_WRITE && receiving) {
    if (mbedtls_sha256_update(&imageSha, raw.buf, raw.currentSize) != 0 ||
        esp_ota_write(writer, raw.buf, raw.currentSize) != ESP_OK) rejectBody(400, "invalid_image");
  } else if (raw.status == RAW_ABORTED) {
    closeWriter();
  }
}

void acceptImage() {
  auto &raw = http->raw();
  if (!receiving) return;
  uint8_t hash[32];
  char digest[65];
  if (raw.status != RAW_END || raw.totalSize != static_cast<size_t>(http->clientContentLength()) ||
      mbedtls_sha256_finish(&imageSha, hash) != 0) {
    rejectBody(400, "incomplete_image");
    return;
  }
  hex(hash, sizeof(hash), digest);
  if (http->header("X-App-SHA256") != digest) {
    rejectBody(400, "digest_mismatch");
    return;
  }
  mbedtls_sha256_free(&imageSha);
  receiving = false;
  if (esp_ota_end(writer) != ESP_OK || !screenIdle() ||
      esp_ota_set_boot_partition(target) != ESP_OK) {
    closeWriter();
    reply(400, "image_not_bootable");
    return;
  }
  reply(202, "restarting");
  delay(250);
  esp_restart();
}

void confirmImage() {
  if (!authorized("confirm", "")) {
    reply(401, "unauthorized");
    return;
  }
  if (!pending.load() || !hardwareOk) {
    reply(409, "not_ready_to_confirm");
    return;
  }
  if (esp_ota_mark_app_valid_cancel_rollback() != ESP_OK) {
    reply(500, "confirmation_failed");
    return;
  }
  pending.store(false);
  reply(200, "confirmed");
}
}  // namespace

// Arduino otherwise confirms the new image before the board's setup runs.
extern "C" bool verifyRollbackLater() {
  return true;
}

bool otaTrialBoot() {
  const esp_partition_t *partition = esp_ota_get_running_partition();
  esp_ota_img_states_t state;
  return partition && esp_ota_get_state_partition(partition, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

void initPanelOta(bool hardwareReady) {
  hardwareOk = hardwareReady;
  running = esp_ota_get_running_partition();
  pending.store(otaTrialBoot());
  bootAt = millis();
  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK) {
    snprintf(device, sizeof(device), "%02x:%02x:%02x:%02x:%02x:%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  }
}

void registerPanelOta(WebServer &server, bool (*panelIdle)()) {
  http = &server;
  screenIdle = panelIdle;
  server.on("/ota/status", HTTP_GET, sendInfo);
  server.on("/ota/app", HTTP_PUT, acceptImage, readImage);
  server.on("/ota/confirm", HTTP_POST, confirmImage);
}

void setPanelOtaKey(const uint8_t *key) {
  nonce[0] = 0;
  keySet = device[0] && key;
  if (keySet) memcpy(authKey, key, sizeof(authKey));
  else memset(authKey, 0, sizeof(authKey));
}

bool otaPending() {
  return pending.load();
}

void tickOta() {
  if (pending.load() && millis() - bootAt >= ConfirmMs) {
    // On a pending image, rebooting without validation selects the prior app.
    esp_restart();
  }
}
