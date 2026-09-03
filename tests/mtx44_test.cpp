#include <dolphin/mtx.h>
#include <dolphin/mtx/mtx44ext.h>

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}
void identity() {
  struct Storage { std::uint32_t before; Mtx44 matrix; std::uint32_t after; } storage;
  std::memset(&storage, 0xff, sizeof(storage));
  PSMTX44Identity(storage.matrix);
  require(storage.before == 0xffffffff && storage.after == 0xffffffff, "identity wrote outside the matrix");
  for (int row = 0; row < 4; ++row) {
    for (int column = 0; column < 4; ++column) {
      require(std::bit_cast<std::uint32_t>(storage.matrix[row][column]) == (row == column ? 0x3f800000u : 0u),
              "identity must write all diagonal ones and positive zero elsewhere");
    }
  }
}
void copy_bits() {
  const std::array<std::uint32_t, 16> input{
      0, 0x80000000, 0x3f800000, 0xbf800000, 0x00800000, 0x80800000, 0x7f7fffff, 0xff7fffff,
      0x00000001, 0x80000001, 0x007fffff, 0x807fffff, 0x7f800000, 0xff800000, 0x7fc12345, 0xff812345};
  const std::array<std::uint32_t, 16> expected{
      0, 0x80000000, 0x3f800000, 0xbf800000, 0x00800000, 0x80800000, 0x7f7fffff, 0xff7fffff,
      0, 0x80000000, 0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345, 0xff812345};
  Mtx44 source, destination;
  std::memcpy(source, input.data(), sizeof(source));
  PSMTX44Copy(source, destination);
  require(std::memcmp(destination, expected.data(), sizeof(destination)) == 0,
          "copy must preserve float bits except signed subnormal flush on quantized stores");
  require(std::memcmp(source, input.data(), sizeof(source)) == 0, "copy modified distinct source");
  PSMTX44Copy(source, source);
  require(std::memcmp(source, expected.data(), sizeof(source)) == 0,
          "in-place copy still performs the retail quantized stores");
}
void forward_overlap() {
  std::array<float, 20> storage;
  for (int i = 0; i < 20; ++i) storage[i] = static_cast<float>(i);
  PSMTX44Copy(reinterpret_cast<const float (*)[4]>(storage.data()),
              reinterpret_cast<float (*)[4]>(storage.data() + 1));
  const std::array<float, 20> expected{0, 0, 1, 1, 3, 3, 5, 5, 7, 7, 9, 9, 11, 11, 13, 13, 15, 17, 18, 19};
  require(storage == expected, "forward overlap must retain each loaded pair, without snapshotting the full matrix");
}
void reverse_overlap() {
  std::array<float, 20> storage;
  for (int i = 0; i < 20; ++i) storage[i] = static_cast<float>(i);
  PSMTX44Copy(reinterpret_cast<const float (*)[4]>(storage.data() + 1),
              reinterpret_cast<float (*)[4]>(storage.data()));
  for (int i = 0; i < 16; ++i) require(storage[i] == i + 1.0f, "reverse overlap lost unread source values");
  for (int i = 16; i < 20; ++i) require(storage[i] == static_cast<float>(i), "copy wrote beyond its 16 output floats");
}
}
int main() {
  try {
    identity(); std::puts("PASS matrix44 identity and write bounds");
    copy_bits(); std::puts("PASS matrix44 quantized float bits and in-place copy");
    forward_overlap(); std::puts("PASS matrix44 forward paired overlap");
    reverse_overlap(); std::puts("PASS matrix44 reverse paired overlap");
  } catch (const std::exception& error) {
    std::fprintf(stderr, "FAIL %s\n", error.what());
    return 1;
  }
}
