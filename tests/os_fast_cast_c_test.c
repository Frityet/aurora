#include <dolphin/os/OSFastCast.h>

int os_fast_cast_c_probe(void) {
  f32 value = 65536.0f;
  u8 byte;
  u16 half;
  s8 signed_byte;
  s16 signed_half;
  OSInitFastCast();
  OSf32tou8(&value, &byte);
  OSf32tou16(&value, &half);
  OSf32tos8(&value, &signed_byte);
  OSf32tos16(&value, &signed_half);
  return byte == 255 && half == 65535 && signed_byte == 127 && signed_half == 32767;
}
