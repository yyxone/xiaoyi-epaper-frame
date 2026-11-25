#pragma once
#include <stddef.h>
#include <stdint.h>

// Slots refer to the album revision that the browser opened for editing.
struct AlbumOrder {
  uint8_t slots[12] = {};
  size_t count = 0;

  bool parse(const char* text, size_t existing) {
    count = 0;
    if (existing > sizeof(slots)) return false;
    unsigned seen = 0;
    while (*text) {
      if (*text < '0' || *text > '9' || count >= existing) return false;
      unsigned slot = *text++ - '0';
      if (*text >= '0' && *text <= '9') {
        if (!slot) return false;
        slot = slot * 10 + (*text++ - '0');
      }
      if (slot >= existing || (seen & (1u << slot))) return false;
      seen |= 1u << slot;
      slots[count++] = slot;
      if (!*text) break;
      if (*text++ != ',' || !*text) return false;
    }
    return count >= 2;
  }

  int position(int oldSlot) const {
    for (size_t i = 0; i < count; ++i)
      if (slots[i] == oldSlot) return static_cast<int>(i);
    return -1;
  }

  size_t nextAfter(int shown, size_t next, size_t existing) const {
    const int kept = position(shown);
    if (kept >= 0) return (static_cast<size_t>(kept) + 1) % count;
    for (size_t i = 0; i < existing; ++i) {
      const int found = position((next + i) % existing);
      if (found >= 0) return static_cast<size_t>(found);
    }
    return 0;
  }
};
