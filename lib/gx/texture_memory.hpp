#pragma once

#include "gx.hpp"
#include "../dolphin/os/internal.hpp"

#include <dolphin/os.h>
#include <optional>

namespace aurora::gx {
constexpr size_t TextureMemorySize = 1024 * 1024;

// Raw BP addresses name MEM1 bytes, never truncated host pointers. The resource
// owner must retain the allocation until commands that read it have completed.
inline const u8* physical_texture_range(u32 address, size_t size) noexcept {
  if (MEM1Start == nullptr || MEM1End == nullptr) {
    return nullptr;
  }
  const auto begin = reinterpret_cast<uintptr_t>(MEM1Start);
  const auto end = reinterpret_cast<uintptr_t>(MEM1End);
  if (end < begin || address >= end - begin || size > end - begin - address) {
    return nullptr;
  }
  return reinterpret_cast<const u8*>(begin + address);
}

inline std::optional<GXTlutObj_> loaded_tlut(const GXTexObj_& obj) noexcept {
  if (obj.tlutRegion == UINT32_MAX) {
    if (static_cast<size_t>(obj.tlut) >= g_gxState.loadedTluts.size()) {
      return std::nullopt;
    }
    const auto& tlut = g_gxState.loadedTluts[obj.tlut];
    return tlut.data != nullptr ? std::optional{tlut} : std::nullopt;
  }

  u16 entries;
  switch (obj.format()) {
  case GX_TF_C4: entries = 16; break;
  case GX_TF_C8: entries = 256; break;
  case GX_TF_C14X2: entries = 16384; break;
  default: return std::nullopt;
  }
  const size_t offset = (obj.tlutRegion & 0x3FFu) << 9;
  const size_t bytes = static_cast<size_t>(entries) * 2;
  if (offset > g_gxState.textureMemory.size() || bytes > g_gxState.textureMemory.size() - offset) {
    return std::nullopt;
  }
  GXTlutObj_ tlut;
  tlut.tlut = obj.tlutRegion;
  tlut.numEntries = entries;
  tlut.data = g_gxState.textureMemory.data() + offset;
  tlut.format = static_cast<GXTlutFmt>((obj.tlutRegion >> 10) & 3u);
  if (tlut.format > GX_TL_RGB5A3) {
    Module{"aurora::gx::texture"}.fatal("unsupported BP palette format {}", static_cast<u32>(tlut.format));
  }
  // Hardware palette bytes can change through overlapping loads. Content
  // hashing, rather than a native object ID, identifies the sampled contents.
  return tlut;
}
} // namespace aurora::gx
