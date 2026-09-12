#include <dolphin/os/OSFastCast.h>
#include <aurora/ppc_math.hpp>
#include <gtest/gtest.h>

#include <bit>
#include <cstdint>
#include <limits>

extern "C" int os_fast_cast_c_probe(void);

namespace {
void expect_conversions(float input, s32 unsigned8, s32 unsigned16, s32 signed8, s32 signed16) {
  EXPECT_EQ(__OSf32tou8(input), unsigned8);
  EXPECT_EQ(__OSf32tou16(input), unsigned16);
  EXPECT_EQ(__OSf32tos8(input), signed8);
  EXPECT_EQ(__OSf32tos16(input), signed16);
  u8 u8result;
  u16 u16result;
  s8 s8result;
  s16 s16result;
  OSf32tou8(&input, &u8result);
  OSf32tou16(&input, &u16result);
  OSf32tos8(&input, &s8result);
  OSf32tos16(&input, &s16result);
  EXPECT_EQ(u8result, unsigned8);
  EXPECT_EQ(u16result, unsigned16);
  EXPECT_EQ(s8result, signed8);
  EXPECT_EQ(s16result, signed16);
}
}

TEST(OSFastCastTest, FractionalValuesTruncateTowardZeroAndOverflowSaturates) {
  expect_conversions(0.0f, 0, 0, 0, 0);
  expect_conversions(-0.0f, 0, 0, 0, 0);
  expect_conversions(0.999f, 0, 0, 0, 0);
  expect_conversions(-0.999f, 0, 0, 0, 0);
  expect_conversions(1.999f, 1, 1, 1, 1);
  expect_conversions(-1.999f, 0, 0, -1, -1);
  expect_conversions(127.75f, 127, 127, 127, 127);
  expect_conversions(128.25f, 128, 128, 127, 128);
  expect_conversions(-128.75f, 0, 0, -128, -128);
  expect_conversions(255.75f, 255, 255, 127, 255);
  expect_conversions(256.25f, 255, 256, 127, 256);
  expect_conversions(32767.75f, 255, 32767, 127, 32767);
  expect_conversions(-32768.75f, 0, 0, -128, -32768);
  expect_conversions(65535.75f, 255, 65535, 127, 32767);
  expect_conversions(65536.0f, 255, 65535, 127, 32767);
}

TEST(OSFastCastTest, SpecialInputsFollowGekkoQuantizedStoreResults) {
  // Gekko User's Manual 2.3.4.3: NaN, including a negative NaN, saturates
  // positively. Dolphin's native FCVT/clamp implementation is not a NaN oracle.
  for (const auto bits : {0x7f800000u, 0x7fc00000u, 0x7f800001u, 0x7fffffffu,
                          0xffc00000u, 0xff800001u, 0xffffffffu, 0x7f7fffffu}) {
    expect_conversions(std::bit_cast<float>(bits), 255, 65535, 127, 32767);
  }
  for (const auto bits : {0xff800000u, 0xff7fffffu}) {
    expect_conversions(std::bit_cast<float>(bits), 0, 0, -128, -32768);
  }
  for (const auto bits : {0x00000001u, 0x007fffffu, 0x00800000u,
                          0x80000001u, 0x807fffffu, 0x80800000u}) {
    expect_conversions(std::bit_cast<float>(bits), 0, 0, 0, 0);
  }
}

TEST(OSFastCastTest, EveryIntegerFormatRoundTripsExactly) {
  for (s32 value = -32768; value <= 65535; ++value) {
    if (value >= 0) {
      const u16 input = static_cast<u16>(value);
      float output;
      OSu16tof32(&input, &output);
      ASSERT_EQ(output, value);
      ASSERT_EQ(__OSu16tof32(&input), value);
      ASSERT_EQ(__OSf32tou16(output), value);
      if (value <= 255) {
        const u8 input8 = static_cast<u8>(value);
        OSu8tof32(&input8, &output);
        ASSERT_EQ(output, value);
        ASSERT_EQ(__OSu8tof32(&input8), value);
        ASSERT_EQ(__OSf32tou8(output), value);
      }
    }
    if (value <= 32767) {
      const s16 input = static_cast<s16>(value);
      float output;
      OSs16tof32(&input, &output);
      ASSERT_EQ(output, value);
      ASSERT_EQ(__OSs16tof32(&input), value);
      ASSERT_EQ(__OSf32tos16(output), value);
      if (value >= -128 && value <= 127) {
        const s8 input8 = static_cast<s8>(value);
        OSs8tof32(&input8, &output);
        ASSERT_EQ(output, value);
        ASSERT_EQ(__OSs8tof32(&input8), value);
        ASSERT_EQ(__OSf32tos8(output), value);
      }
    }
  }
}

TEST(OSFastCastTest, QuantizedStoresDifferFromFctiwzAndNarrowing) {
  EXPECT_EQ(__OSf32tou16(65536.0f), 65535);
  EXPECT_EQ(aurora::ppc::truncate_u16(65536.0), 0);
  EXPECT_EQ(__OSf32tos16(32768.0f), 32767);
  EXPECT_EQ(aurora::ppc::truncate_s16(32768.0), -32768);
  EXPECT_EQ(__OSf32tos16(std::numeric_limits<float>::quiet_NaN()), 32767);
  EXPECT_EQ(aurora::ppc::truncate_s32(std::numeric_limits<float>::quiet_NaN()),
            std::numeric_limits<std::int32_t>::min());
}

TEST(OSFastCastTest, PublicHeaderWorksFromC) {
  EXPECT_EQ(os_fast_cast_c_probe(), 1);
}
