#pragma once

#include <aurora/allocation.hpp>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace aurora {

// Borrowed native pointers referenced by two 16-bit guest words. Keep the table
// with the owner of the guest data; never truncate a native pointer into it.
template <class T>
class PointerSlots {
public:
  template <class Word>
  void store(Word* words, std::uint32_t slot, T* pointer) {
    static_assert(sizeof(Word) >= sizeof(std::uint16_t));
    if (slot == std::numeric_limits<std::uint32_t>::max()) {
      throw std::out_of_range("Pointer slot cannot be encoded");
    }
    const allocation::HostAllocationScope host;
    const std::uint32_t handle = pointer ? slot + 1 : 0;
    if (pointer) {
      pointers_[slot] = pointer;
    } else {
      pointers_.erase(slot);
    }
    words[0] = static_cast<Word>(handle >> 16);
    words[1] = static_cast<Word>(handle & 0xffff);
  }

  template <class Word>
  T* load(const Word* words) const {
    static_assert(sizeof(Word) >= sizeof(std::uint16_t));
    const std::uint32_t handle = (static_cast<std::uint32_t>(words[0]) << 16) |
                                 static_cast<std::uint16_t>(words[1]);
    if (handle == 0) {
      return nullptr;
    }
    const auto entry = pointers_.find(handle - 1);
    return entry == pointers_.end() ? nullptr : entry->second;
  }

private:
  std::unordered_map<std::uint32_t, T*> pointers_;
};

} // namespace aurora
