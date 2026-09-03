#include <dolphin/gx/GXTexture.h>

#include <cassert>
#include <cstring>

namespace {
// GXTexRegion retains its SDK layout; these are native integer fields, not a
// serialized BP command stream. Cache initialization leaves the preload sizes
// and trailing padding alone, as on the console.
void store_flags(GXTexRegion* region, u8 mipmap, u8 cached) {
  auto* bytes = reinterpret_cast<unsigned char*>(region);
  bytes[12] = mipmap;
  bytes[13] = cached;
}
} // namespace

void GXInitTexCacheRegion(GXTexRegion* region, GXBool is_32b_mipmap, u32 tmem_even, GXTexCacheSize size_even,
                         u32 tmem_odd, GXTexCacheSize size_odd) {
  // The SDK requires an even cache. GX_TEXCACHE_NONE is valid for the odd bank.
  assert(size_even >= GX_TEXCACHE_32K && size_even <= GX_TEXCACHE_512K);
  assert(size_odd >= GX_TEXCACHE_32K && size_odd <= GX_TEXCACHE_NONE);
  const auto even = static_cast<u32>(size_even) + 3;
  const auto odd = size_odd == GX_TEXCACHE_NONE ? 0U : static_cast<u32>(size_odd) + 3;
  region->dummy[0] = ((tmem_even >> 5) & 0x7fff) | (even << 15) | (even << 18);
  region->dummy[1] = ((tmem_odd >> 5) & 0x7fff) | (odd << 15) | (odd << 18);
  store_flags(region, static_cast<u8>(is_32b_mipmap), GX_TRUE);
}

void GXInitTexPreLoadRegion(GXTexRegion* region, u32 tmem_even, u32 size_even, u32 tmem_odd, u32 size_odd) {
  region->dummy[0] = ((tmem_even >> 5) & 0x7fff) | (1U << 21);
  region->dummy[1] = (tmem_odd >> 5) & 0x7fff;
  const auto even = static_cast<u16>(size_even >> 5);
  const auto odd = static_cast<u16>(size_odd >> 5);
  auto* bytes = reinterpret_cast<unsigned char*>(region);
  std::memcpy(bytes + 8, &even, sizeof(even));
  std::memcpy(bytes + 10, &odd, sizeof(odd));
  store_flags(region, 0, GX_FALSE);
}
