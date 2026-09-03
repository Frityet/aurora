#include <aurora/sysconf.hpp>

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition) {
  if (!condition) throw std::runtime_error("SYSCONF test failed");
}

template <class Function> void rejected(Function function) {
  bool threw = false;
  try { function(); } catch (const std::exception&) { threw = true; }
  require(threw);
}

std::vector<std::uint8_t> golden() {
  // Independent compact wire fixture: seven entry kinds with fixed offsets,
  // multibyte scalar values and both array length encodings.
  return {
      'S','C','v','0', 0,7, 0,22, 0,25, 0,29, 0,35, 0,45, 0,48, 0,54, 0,60,
      0x60,'A',0x12,
      0x80,'B',0x34,0x56,
      0xa0,'C',0x78,0x9a,0xbc,0xde,
      0xc0,'D',0x01,0x23,0x45,0x67,0x89,0xab,0xcd,0xef,
      0xe0,'E',1,
      0x40,'F',2,1,2,3,
      0x20,'G',0,1,0xde,0xad,
      'S','C','e','d'};
}
} // namespace

int main() {
  using T = aurora::SysConf::Type;
  auto bytes = golden();
  auto document = aurora::SysConf::decode(bytes);
  require(document.entries().size() == 7);
  require(document.integer("A", T::Byte) == 0x12);
  require(document.integer("B", T::Short) == 0x3456);
  require(document.integer("C", T::Long) == 0x789abcde);
  require(document.integer("D", T::LongLong) == 0x0123456789abcdefULL);
  require(document.integer("E", T::Bool) == 1);
  require(!document.integer("E", T::Byte));
  require(!document.integer("F", T::SmallArray));
  require(!document.integer("missing", T::Byte));
  require(document.find("F")->data == std::vector<std::uint8_t>({1,2,3}));
  require(document.find("G")->data == std::vector<std::uint8_t>({0xde,0xad}));
  const auto encoded = document.encode();
  require(encoded.size() == aurora::SysConf::FileSize);
  require(std::equal(bytes.begin(), bytes.begin() + 60, encoded.begin()));
  require(std::ranges::equal(aurora::SysConf::decode(encoded).entries(), document.entries()));
  bytes.assign(bytes.size(), 0); // The decoded document owns every entry.
  require(document.integer("C", T::Long) == 0x789abcde);

  document.replace_integer("D", T::LongLong, 0xffffffffffffffffULL);
  document.replace_integer("A", T::Byte, 255);
  document.replace({T::SmallArray, "F", std::vector<std::uint8_t>(256, 0x77)});
  document.replace({T::BigArray, "G", std::vector<std::uint8_t>(257, 0x88)});
  document.replace_integer("custom.key", T::Long, 0xfedcba98);
  auto changed = aurora::SysConf::decode(document.encode());
  require(changed.integer("D", T::LongLong) == 0xffffffffffffffffULL);
  require(changed.integer("custom.key", T::Long) == 0xfedcba98);
  require(changed.find("F")->data.size() == 256);
  require(changed.find("G")->data.size() == 257);
  const auto snapshot = changed.encode();
  rejected([&] { changed.replace_integer("A", T::Byte, 256); });
  rejected([&] { changed.replace_integer("A", T::SmallArray, 1); });
  rejected([&] { changed.replace({T::Byte, "", {1}}); });
  rejected([&] { changed.replace({T::Byte, std::string(33, 'x'), {1}}); });
  rejected([&] { changed.replace({T::Long, "A", {1}}); });
  rejected([&] { changed.replace({T::SmallArray, "A", std::vector<std::uint8_t>(257)}); });
  rejected([&] { changed.replace({T::BigArray, "A", std::vector<std::uint8_t>(16384)}); });
  require(changed.encode() == snapshot);
  require(changed.erase("A") && !changed.erase("A") && changed.find("A") == nullptr);

  std::size_t malformed = 0;
  const auto corrupt = [&](std::size_t at, std::uint8_t value) {
    auto input = golden(); input[at] = value;
    rejected([&] { (void)aurora::SysConf::decode(input); });
    ++malformed;
  };
  corrupt(0, 0); corrupt(63, 0); corrupt(4, 255); corrupt(7, 21);
  corrupt(22, 0); corrupt(48, 0x5f); corrupt(50, 255); corrupt(56, 255); corrupt(21, 59);
  rejected([] { (void)aurora::SysConf::decode(std::array<std::uint8_t, 11>{}); });
  rejected([] { (void)aurora::SysConf::decode(std::vector<std::uint8_t>(16385)); });

  auto duplicate = golden(); duplicate[55] = 'A';
  auto duplicates = aurora::SysConf::decode(duplicate);
  require(duplicates.entries().size() == 7 && duplicates.integer("A", T::Byte) == 0x12);
  require(duplicates.erase("A") && duplicates.find("A")->type == T::BigArray);
  require(aurora::SysConf::decode(aurora::SysConf{}.encode()).entries().empty());
  std::cout << "SYSCONF: seven wire types, owned roundtrip, exact integer types, array boundaries, "
               "capacity rollback, duplicate order, and " << malformed + 2 << " malformed inputs pass\n";
}
