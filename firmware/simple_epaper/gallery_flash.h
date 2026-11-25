#pragma once

#include <esp_partition.h>
#include <stddef.h>
#include <stdint.h>

struct SavedAlbum {
  uint32_t sequence = 0, count = 0, interval = 0, next = 0;
};

struct SavedScreen {
  uint32_t albumSequence = 0, slot = UINT32_MAX, next = 0;
  char sha[65] = {};
};

// Raw storage in an explicitly blank or already owned data partition. The active
// album bank is never erased until a different bank has a valid final record.
class GalleryFlash {
 public:
  bool begin();
  bool ready() const { return volume != nullptr; }
  bool readAlbum(uint8_t *frames, SavedAlbum &result);
  bool saveAlbum(const uint8_t *frames, uint32_t count, uint32_t interval, uint32_t next);
  bool readScreen(uint8_t *frame, SavedScreen &result);
  bool saveScreen(const uint8_t *frame, const SavedScreen &snapshot);
  uint32_t albumSequence() const { return albumSerial; }

 private:
  struct Record {
    uint32_t sequence, count, interval, next, album, slot, length, payloadCrc;
    char sha[68];
    uint32_t headerCrc;
  };
  const esp_partition_t *volume = nullptr;
  bool owned = false;
  uint32_t albumSerial = 0, screenSerial = 0;
  uint8_t block[4096];  // Internal RAM buffer for flash I/O with PSRAM-backed frames.

  bool claim();
  bool getRecord(size_t position, Record &record);
  bool checkPayload(size_t position, const Record &record);
  bool copyPayload(size_t position, uint8_t *out, size_t length);
  bool commit(size_t position, const uint8_t *data, Record &record);
};
