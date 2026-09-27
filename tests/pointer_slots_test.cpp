#include <aurora/pointer_slots.hpp>
#include <array>
#include <cstdlib>
#include <limits>

namespace {
void check(bool condition) {
  if (!condition) std::abort();
}

template <class Word>
void test_word_width() {
  const wchar_t first[] = L"first";
  const wchar_t second[] = L"second";
  aurora::PointerSlots<const wchar_t> slots;
  aurora::PointerSlots<const wchar_t> otherOwner;
  std::array<Word, 6> tag{Word{0x1234}, Word{0}, Word{0}, Word{0x4321}, Word{0x5678}, Word{0}};
  slots.store(tag.data() + 1, 0x10002, first);
  check(tag[0] == Word{0x1234} && tag[3] == Word{0x4321} && tag[4] == Word{0x5678} && tag[5] == 0);
  check(slots.load(tag.data() + 1) == first);
  check(otherOwner.load(tag.data() + 1) == nullptr);
  auto copy = tag;
  check(slots.load(copy.data() + 1) == first);
  slots.store(tag.data() + 1, 0x10002, second);
  check(slots.load(tag.data() + 1) == second);
  check(slots.load(copy.data() + 1) == second);
  slots.store(tag.data() + 1, 0x10002, nullptr);
  check(tag[1] == 0 && tag[2] == 0);
  check(slots.load(tag.data() + 1) == nullptr && slots.load(copy.data() + 1) == nullptr);
  const auto before = tag;
  bool rejected = false;
  try {
    slots.store(tag.data() + 1, std::numeric_limits<std::uint32_t>::max(), first);
  } catch (const std::out_of_range&) {
    rejected = true;
  }
  check(rejected && tag == before);
}
} // namespace

int main() {
  test_word_width<char16_t>();
  test_word_width<char32_t>();
  test_word_width<wchar_t>();
}
