#pragma once

#include <aurora/endian.hpp>

#include <bit>
#include <cstdint>
#include <limits>

namespace aurora::resource {
// A resource's 32-bit file offset becomes a signed displacement from this field
// when its original SDK loader relocates it. The record keeps its Wii size, and
// the resulting pointer can address the entire native allocation on 64-bit hosts.
// Null is zero. Relocated records must stay with their referenced resource bytes;
// copying the complete resource preserves every displacement.
template <typename T>
struct RelocatedPtr32 {
  endian::BigEndian<std::uint32_t> offset;

  T* get() const noexcept {
    const auto displacement = std::bit_cast<std::int32_t>(std::uint32_t(offset));
    if (displacement == 0) return nullptr;
    return reinterpret_cast<T*>(reinterpret_cast<std::uintptr_t>(this) +
                                static_cast<std::intptr_t>(displacement));
  }

  operator T*() const noexcept { return get(); }
  T* operator->() const noexcept { return get(); }

  void relocate(const void* base) {
    const auto fileOffset = std::uint32_t(offset);
    if (fileOffset == 0) return;
    const auto target = reinterpret_cast<std::uintptr_t>(base) + fileOffset;
    const auto field = reinterpret_cast<std::uintptr_t>(this);
    const auto displacement = target >= field ? std::int64_t(target - field) : -std::int64_t(field - target);
    if (displacement == 0 || displacement < std::numeric_limits<std::int32_t>::min() ||
        displacement > std::numeric_limits<std::int32_t>::max()) {
      aurora::throw_host_exception<std::runtime_error>("Resource pointer exceeds its signed 32-bit displacement");
    }
    offset = static_cast<std::uint32_t>(displacement);
  }
};

static_assert(sizeof(RelocatedPtr32<std::uint8_t>) == 4);
static_assert(alignof(RelocatedPtr32<std::uint8_t>) == 1);
} // namespace aurora::resource
