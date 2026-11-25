#include "gallery_flash.h"
#include "frame_rules.h"
#include "album_order.h"
#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <vector>

static const esp_partition_t partition{0x9e0000};
static std::vector<uint8_t> contents(partition.size,0xff);
static int writesUntilCut = -1;

const esp_partition_t *esp_partition_find_first(int type,int subtype,const char *label) {
  return type==1 && subtype==129 && strcmp(label,"ffat")==0 ? &partition : nullptr;
}
int esp_partition_read(const esp_partition_t *,size_t offset,void *out,size_t n) {
  assert(offset+n<=contents.size());
  memcpy(out,contents.data()+offset,n);
  return ESP_OK;
}
int esp_partition_write(const esp_partition_t *,size_t offset,const void *data,size_t n) {
  assert(offset+n<=contents.size());
  const bool cut = writesUntilCut == 0;
  if (writesUntilCut >= 0) --writesUntilCut;
  const auto *bytes = static_cast<const uint8_t *>(data);
  for (size_t i=0; i<(cut ? n/2 : n); ++i) {
    assert((contents[offset+i] & bytes[i])==bytes[i]);
    contents[offset+i] &= bytes[i];
  }
  return cut ? -1 : ESP_OK;
}
int esp_partition_erase_range(const esp_partition_t *,size_t offset,size_t n) {
  assert(offset%4096==0 && n%4096==0 && offset+n<=contents.size());
  std::fill(contents.begin()+offset,contents.begin()+offset+n,0xff);
  return ESP_OK;
}
uint32_t esp_rom_crc32_le(uint32_t crc,const uint8_t *bytes,uint32_t n) {
  crc = ~crc;
  for (uint32_t i=0;i<n;++i) {
    crc ^= bytes[i];
    for (unsigned bit=0;bit<8;++bit) crc = (crc>>1) ^ ((0U-(crc&1U)) & 0xedb88320U);
  }
  return ~crc;
}
int main() {
  AlbumOrder order;
  for (const char *invalid : {"", "1", "0,0", "1,2,", " 1,0", "01,2", "1,12", "0;2", "0,999"})
    assert(!order.parse(invalid,12));
  assert(order.parse("11,0,5,2",12));
  assert(order.count==4 && order.position(11)==0 && order.position(1)==-1);
  assert(order.nextAfter(5,6,12)==3 && order.nextAfter(-1,7,12)==0);
  assert(order.nextAfter(8,1,12)==3 && order.nextAfter(2,3,12)==0);
  contents.back() = 0x12;
  GalleryFlash foreign;
  assert(!foreign.begin() && contents.back()==0x12);
  contents.back() = 0xff;

  constexpr uint32_t interval = 30*60000;
  std::vector<uint8_t> original(FrameBytes*2,0x23), updated(FrameBytes*2,0x41), restored(FrameBytes*2);
  GalleryFlash initial;
  assert(initial.begin() && initial.saveAlbum(original.data(),2,interval,1));
  // Interrupted payload and interrupted final commit both leave the last good bank available.
  for (int cut : {3,94}) {
    writesUntilCut = cut;
    assert(!initial.saveAlbum(updated.data(),2,interval,0));
    writesUntilCut = -1;
    GalleryFlash restarted;
    SavedAlbum old;
    assert(restarted.begin() && restarted.readAlbum(restored.data(),old));
    assert(restored==original && old.count==2 && old.interval==interval && old.next==1);
  }
  assert(initial.saveAlbum(updated.data(),2,interval,0));
  SavedScreen current;
  current.albumSequence=initial.albumSequence();
  current.slot=0;
  current.next=1;
  memset(current.sha,'a',64);
  for (int i=0;i<25;++i) assert(initial.saveScreen(updated.data(),current));
  GalleryFlash resumed;
  SavedAlbum playlist;
  SavedScreen last;
  assert(resumed.begin() && resumed.readAlbum(restored.data(),playlist));
  assert(restored==updated && playlist.next==0);
  assert(resumed.readScreen(restored.data(),last));
  assert(last.albumSequence==playlist.sequence && last.slot==0 && last.next==1 && last.sha[63]=='a');
  assert(resumed.saveAlbum(nullptr,0,0,0));
  GalleryFlash cleared;
  SavedAlbum empty;
  assert(cleared.begin() && cleared.readAlbum(restored.data(),empty) && empty.count==0);
  assert(cleared.readScreen(restored.data(),last) && last.albumSequence!=empty.sequence);
  puts("PASS: foreign partition preserved, incomplete payload and commit survive power cut, playlist and position restore, snapshot ring wraps, clear stays cleared");
}
