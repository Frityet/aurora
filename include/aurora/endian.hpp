#pragma once

#include <concepts>
#include <cstddef>
#include <cstdint>

namespace aurora::endian {
// Packed Wii resources and GX command streams retain their original byte order.
template <std::unsigned_integral T>
T read_big(const void* source) noexcept {
  const auto* bytes = static_cast<const std::uint8_t*>(source);
  T value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value = static_cast<T>((value << 8) | bytes[i]);
  }
  return value;
}

template <std::unsigned_integral T>
void write_big(void* destination, T value) noexcept {
  auto* bytes = static_cast<std::uint8_t*>(destination);
  for (std::size_t i = sizeof(T); i != 0; --i) {
    bytes[i - 1] = static_cast<std::uint8_t>(value);
    value >>= 8;
  }
}

inline std::uint16_t read_u16(const void* source) noexcept { return read_big<std::uint16_t>(source); }
inline std::uint32_t read_u32(const void* source) noexcept { return read_big<std::uint32_t>(source); }
inline void write_u16(void* destination, std::uint16_t value) noexcept { write_big(destination, value); }

// Word-addressed CPU writes into a byte buffer must produce the same bytes as a
// Wii store. No alignment is required, and no native-endian word aliases the data.
template <std::unsigned_integral T>
struct BigEndian {
  std::uint8_t bytes[sizeof(T)];

  operator T() const noexcept { return read_big<T>(bytes); }
  BigEndian& operator=(T value) noexcept {
    write_big(bytes, value);
    return *this;
  }
};

static_assert(sizeof(BigEndian<std::uint16_t>) == sizeof(std::uint16_t));
static_assert(alignof(BigEndian<std::uint16_t>) == 1);
} // namespace aurora::endian
