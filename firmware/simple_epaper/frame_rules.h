#pragma once
#include <stddef.h>
#include <stdint.h>
constexpr size_t FrameBytes = 800u * 480u / 2u;
inline bool hexLower(const char *text, size_t size) {
  for (size_t i=0;i<size;++i) if (!((text[i]>='0'&&text[i]<='9')||(text[i]>='a'&&text[i]<='f'))) return false;
  return true;
}
inline uint8_t hexDigit(char c) { return c <= '9' ? c-'0' : c-'a'+10; }
inline bool validFrame(const uint8_t *p, size_t size) {
  if (!p || size != FrameBytes) return false;
  for (size_t i=0;i<size;++i) if ((p[i]>>4)>5 || (p[i]&15)>5) return false;
  return true;
}
inline void nativeFrame(uint8_t *p, size_t size) {
  constexpr uint8_t native[] = {0,1,2,3,5,6};
  for (size_t i=0;i<size;++i) p[i] = (native[p[i]>>4]<<4)|native[p[i]&15];
}
