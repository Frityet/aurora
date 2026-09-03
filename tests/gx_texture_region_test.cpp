#include <dolphin/gx/GXTexture.h>
#include <gtest/gtest.h>

#include <array>
#include <cstring>

TEST(GXTextureRegion, CacheBanksAndUntouchedFields) {
  constexpr std::array<u32, 3> cache{0x000d8000, 0x00120000, 0x00168000};
  for (u32 even = 0; even != cache.size(); ++even) {
    for (u32 odd = 0; odd != 4; ++odd) {
      GXTexRegion region;
      std::memset(&region, 0xa5, sizeof(region));
      GXInitTexCacheRegion(&region, GX_TRUE, 0x28000, static_cast<GXTexCacheSize>(even), 0x80000,
                          static_cast<GXTexCacheSize>(odd));
      EXPECT_EQ(region.dummy[0], cache[even] | 0x1400);
      EXPECT_EQ(region.dummy[1], (odd == 3 ? 0 : cache[odd]) | 0x4000);
      EXPECT_EQ(region.dummy[2], 0xa5a5a5a5);
      const auto* bytes = reinterpret_cast<const u8*>(&region);
      EXPECT_EQ(bytes[12], 1);
      EXPECT_EQ(bytes[13], 1);
      EXPECT_EQ(bytes[14], 0xa5);
      EXPECT_EQ(bytes[15], 0xa5);
    }
  }
}

TEST(GXTextureRegion, AddressFieldsUseOriginalBitWidths) {
  GXTexRegion region{};
  GXInitTexCacheRegion(&region, GX_TRUE, 0xffffffff, GX_TEXCACHE_32K, 0x1234567f,
                      GX_TEXCACHE_NONE);
  EXPECT_EQ(region.dummy[0], 0x000dffff);
  EXPECT_EQ(region.dummy[1], 0x22b3);
  EXPECT_EQ(reinterpret_cast<const u8*>(&region)[12], 1);
}

TEST(GXTextureRegion, PreloadSizesAndCacheTransition) {
  GXTexRegion region;
  std::memset(&region, 0xa5, sizeof(region));
  GXInitTexPreLoadRegion(&region, 0x80000, 0x12345678, 0x1234567f, 0xffffffff);
  EXPECT_EQ(region.dummy[0], 0x00204000);
  EXPECT_EQ(region.dummy[1], 0x22b3);
  std::array<u16, 2> sizes;
  const auto* bytes = reinterpret_cast<const u8*>(&region);
  std::memcpy(sizes.data(), bytes + 8, sizeof(sizes));
  EXPECT_EQ(sizes[0], 0xa2b3);
  EXPECT_EQ(sizes[1], 0xffff);
  EXPECT_EQ(bytes[12], 0);
  EXPECT_EQ(bytes[13], 0);
  EXPECT_EQ(bytes[14], 0xa5);
  EXPECT_EQ(bytes[15], 0xa5);
  const auto retained_sizes = region.dummy[2];
  GXInitTexCacheRegion(&region, GX_FALSE, 0, GX_TEXCACHE_32K, 0x80000, GX_TEXCACHE_32K);
  EXPECT_EQ(region.dummy[2], retained_sizes);
  EXPECT_EQ(bytes[12], 0);
  EXPECT_EQ(bytes[13], 1);
}
