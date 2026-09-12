#include <aurora/endian.hpp>

#include <array>
#include <bit>
#include <cstdint>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>

namespace {
unsigned checks = 0;

void require(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

template <typename F>
void rejects(F call) {
  bool rejected = false;
  try { call(); } catch (const std::runtime_error&) { rejected = true; }
  require(rejected, "out-of-range endian read was accepted");
}

template <typename T>
void bounds() {
  const std::array<std::uint8_t, 16> bytes{};
  const auto source = std::span<const std::uint8_t>(bytes);
  require(aurora::endian::read_big<T>(source, source.size() - sizeof(T)) == T{}, "last complete scalar was rejected");
  rejects([&] { (void)aurora::endian::read_big<T>(source, source.size() - sizeof(T) + 1); });
  rejects([&] { (void)aurora::endian::read_big<T>(source, source.size()); });
  rejects([&] { (void)aurora::endian::read_big<T>(source, std::numeric_limits<std::size_t>::max()); });
  rejects([&] { (void)aurora::endian::read_big<T>(source, std::numeric_limits<std::size_t>::max() - sizeof(T) + 1); });
  rejects([&] { (void)aurora::endian::read_big<T>(source.first(sizeof(T) - 1), 0); });
  rejects([&] { (void)aurora::endian::read_big<T>(std::span<const std::uint8_t>{}, 0); });
}
} // namespace

int main() {
  using aurora::endian::read_big;
  try {
    const std::array<std::uint8_t, 9> bytes{0xaa, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10};
    const auto source = std::span<const std::uint8_t>(bytes);
    require(read_big<std::uint8_t>(source, 1) == 0xfe, "u8 decode");
    require(read_big<std::int8_t>(source, 1) == -2, "s8 decode");
    require(read_big<std::uint16_t>(source, 1) == 0xfedc, "unaligned u16 decode");
    require(read_big<std::int16_t>(source, 1) == -292, "signed 16-bit decode");
    require(read_big<std::uint32_t>(source, 1) == 0xfedcba98U, "unaligned u32 decode");
    require(read_big<std::int32_t>(source, 1) == -19088744, "signed 32-bit decode");
    require(read_big<std::uint64_t>(source, 1) == 0xfedcba9876543210ULL, "unaligned u64 decode");
    require(read_big<std::int64_t>(source, 1) == -81985529216486896LL, "signed 64-bit decode");
    require(aurora::endian::read_u16(bytes.data() + 1) == 0xfedc, "existing pointer u16 entry point");
    require(aurora::endian::read_u32(bytes.data() + 1) == 0xfedcba98U, "existing pointer u32 entry point");

    const std::array<std::uint32_t, 9> float_patterns{
        0, 0x80000000U, 0x3f800000U, 0xc0200000U, 1, 0x7f800000U, 0xff800000U, 0x7fc12345U, 0x7fa12345U};
    for (auto bits : float_patterns) {
      const std::array<std::uint8_t, 5> encoded{
          0xaa, std::uint8_t(bits >> 24), std::uint8_t(bits >> 16), std::uint8_t(bits >> 8), std::uint8_t(bits)};
      require(std::bit_cast<std::uint32_t>(read_big<float>(encoded, 1)) == bits, "float payload changed");
    }
    const std::array<std::uint8_t, 9> double_nan{0xaa, 0x7f, 0xf8, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    require(std::bit_cast<std::uint64_t>(read_big<double>(double_nan, 1)) == 0x7ff8123456789abcULL,
            "double payload changed");

    std::array<std::uint8_t, 6> encoded{0xaa, 0, 0, 0, 0, 0xbb};
    aurora::endian::write_big(encoded.data() + 1, std::uint32_t{0x12345678});
    require(encoded == std::array<std::uint8_t, 6>{0xaa, 0x12, 0x34, 0x56, 0x78, 0xbb}, "existing unaligned writer changed");
    aurora::endian::BigEndian<std::uint16_t> packed{};
    packed = 0x1234;
    require(packed.bytes[0] == 0x12 && packed.bytes[1] == 0x34 && std::uint16_t(packed) == 0x1234,
            "existing packed scalar changed");

    bounds<std::uint8_t>(); bounds<std::int8_t>();
    bounds<std::uint16_t>(); bounds<std::int16_t>();
    bounds<std::uint32_t>(); bounds<std::int32_t>();
    bounds<std::uint64_t>(); bounds<std::int64_t>();
    bounds<float>(); bounds<double>();
    std::cout << "PASS " << checks << " endian checks: signed/unsigned, unaligned, floating payloads, bounds and overflow\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
