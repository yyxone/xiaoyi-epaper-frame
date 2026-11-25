#include "gallery_flash.h"

#include <Arduino.h>
#include <esp_rom_crc.h>
#include <string.h>
#include "frame_rules.h"

namespace {
constexpr size_t Sector = 4096;
constexpr size_t AlbumStride = 0x240000;
constexpr size_t ScreenStride = 0x30000;
constexpr size_t MaxSavedFrames = 12;
constexpr uint32_t ScreenSlots = 24;
constexpr size_t ScreenStart = Sector + 2 * AlbumStride;
constexpr size_t Needed = ScreenStart + ScreenSlots * ScreenStride;
constexpr char Marker[] = "xiaoyi-gallery-frames-v1";
static_assert(Sector + MaxSavedFrames * FrameBytes <= AlbumStride);
static_assert(Sector + FrameBytes <= ScreenStride);

size_t albumOffset(uint32_t sequence) { return Sector + (sequence % 2) * AlbumStride; }
size_t screenOffset(uint32_t sequence) { return ScreenStart + (sequence % ScreenSlots) * ScreenStride; }
bool intervalValid(uint32_t value) {
  return value == 15UL*60000 || value == 30UL*60000 || value == 60UL*60000;
}
}

bool GalleryFlash::begin() {
  const esp_partition_t *candidate = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT, "ffat");
  if (!candidate || candidate->size < Needed) return false;
  char marker[sizeof(Marker)];
  if (esp_partition_read(candidate, 0, marker, sizeof(marker)) != ESP_OK) return false;
  if (memcmp(marker, Marker, sizeof(Marker)) == 0) {
    volume = candidate;
    owned = true;
    return true;
  }
  // Existing FAT files or an unrecognized layout take precedence over this feature.
  for (size_t position=0; position<candidate->size; position+=Sector) {
    const size_t n = min(Sector, static_cast<size_t>(candidate->size)-position);
    if (esp_partition_read(candidate, position, block, n) != ESP_OK) return false;
    for (size_t i=0; i<n; ++i) if (block[i] != 0xff) return false;
    yield();
  }
  volume = candidate;
  return true;
}

bool GalleryFlash::claim() {
  if (!volume) return false;
  if (owned) return true;
  char check[sizeof(Marker)];
  owned = esp_partition_write(volume, 0, Marker, sizeof(Marker)) == ESP_OK &&
          esp_partition_read(volume, 0, check, sizeof(check)) == ESP_OK &&
          memcmp(check, Marker, sizeof(check)) == 0;
  return owned;
}

bool GalleryFlash::getRecord(size_t position, Record &record) {
  return esp_partition_read(volume, position, &record, sizeof(record)) == ESP_OK &&
         record.sequence != 0 && record.sequence != UINT32_MAX &&
         record.headerCrc == esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&record), offsetof(Record, headerCrc));
}

bool GalleryFlash::checkPayload(size_t position, const Record &record) {
  uint32_t crc = 0;
  for (size_t done=0; done<record.length; done+=Sector) {
    const size_t n = min(Sector, static_cast<size_t>(record.length)-done);
    if (esp_partition_read(volume, position+Sector+done, block, n) != ESP_OK) return false;
    crc = esp_rom_crc32_le(crc, block, n);
    yield();
  }
  return crc == record.payloadCrc;
}

bool GalleryFlash::copyPayload(size_t position, uint8_t *out, size_t length) {
  for (size_t done=0; done<length; done+=Sector) {
    const size_t n = min(Sector,length-done);
    if (esp_partition_read(volume, position+Sector+done, block, n) != ESP_OK) return false;
    memcpy(out+done, block, n);
    yield();
  }
  return true;
}

bool GalleryFlash::commit(size_t position, const uint8_t *data, Record &record) {
  if (!claim()) return false;
  const size_t span = Sector + ((record.length + Sector-1) / Sector)*Sector;
  if (esp_partition_erase_range(volume, position, span) != ESP_OK) return false;
  for (size_t done=0; done<record.length; done+=Sector) {
    const size_t n = min(Sector,static_cast<size_t>(record.length)-done);
    memcpy(block, data+done, n);
    record.payloadCrc = esp_rom_crc32_le(record.payloadCrc, block, n);
    if (esp_partition_write(volume, position+Sector+done, block, n) != ESP_OK) return false;
    yield();
  }
  if (!checkPayload(position, record)) return false;
  record.headerCrc = esp_rom_crc32_le(0, reinterpret_cast<const uint8_t *>(&record), offsetof(Record, headerCrc));
  Record confirmed = {};
  return esp_partition_write(volume, position, &record, sizeof(record)) == ESP_OK &&
         getRecord(position, confirmed) && confirmed.headerCrc == record.headerCrc;
}

bool GalleryFlash::readAlbum(uint8_t *frames, SavedAlbum &result) {
  if (!owned || !frames) return false;
  Record best = {};
  for (uint32_t index=0; index<2; ++index) {
    const size_t position = Sector + index*AlbumStride;
    Record entry = {};
    if (!getRecord(position, entry) || albumOffset(entry.sequence) != position || entry.sequence <= best.sequence ||
        entry.count > MaxSavedFrames || entry.count == 1 || entry.length != entry.count*FrameBytes ||
        (entry.count && (!intervalValid(entry.interval) || entry.next >= entry.count)) ||
        !checkPayload(position, entry)) continue;
    best = entry;
  }
  if (!best.sequence || !copyPayload(albumOffset(best.sequence), frames, best.length)) return false;
  albumSerial = best.sequence;
  result = {best.sequence, best.count, best.interval, best.next};
  return true;
}

bool GalleryFlash::saveAlbum(const uint8_t *frames, uint32_t count, uint32_t interval, uint32_t next) {
  if (count > MaxSavedFrames || count == 1 || (count && (!frames || !intervalValid(interval) || next >= count))) return false;
  Record entry = {};
  entry.sequence = albumSerial+1;
  entry.count = count;
  entry.interval = interval;
  entry.next = next;
  entry.length = count*FrameBytes;
  if (!commit(albumOffset(entry.sequence), frames, entry)) return false;
  albumSerial = entry.sequence;
  return true;
}

bool GalleryFlash::readScreen(uint8_t *frame, SavedScreen &result) {
  if (!owned || !frame) return false;
  Record best = {};
  for (uint32_t index=0; index<ScreenSlots; ++index) {
    const size_t position = ScreenStart + index*ScreenStride;
    Record entry = {};
    if (!getRecord(position,entry) || screenOffset(entry.sequence) != position || entry.sequence <= best.sequence ||
        entry.length != FrameBytes || entry.sha[64] || !hexLower(entry.sha,64) || !checkPayload(position,entry)) continue;
    best = entry;
  }
  if (!best.sequence || !copyPayload(screenOffset(best.sequence),frame,best.length)) return false;
  screenSerial = best.sequence;
  result.albumSequence = best.album;
  result.slot = best.slot;
  result.next = best.next;
  memcpy(result.sha,best.sha,sizeof(result.sha));
  return true;
}

bool GalleryFlash::saveScreen(const uint8_t *frame, const SavedScreen &snapshot) {
  if (!frame) return false;
  Record entry = {};
  entry.sequence = screenSerial+1;
  entry.album = snapshot.albumSequence;
  entry.slot = snapshot.slot;
  entry.next = snapshot.next;
  entry.length = FrameBytes;
  memcpy(entry.sha,snapshot.sha,sizeof(snapshot.sha));
  if (!commit(screenOffset(entry.sequence),frame,entry)) return false;
  screenSerial = entry.sequence;
  return true;
}
