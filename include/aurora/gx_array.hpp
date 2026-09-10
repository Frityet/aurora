#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace aurora::gx {
namespace detail {
struct ArrayRegistryState;
}

struct ArrayExtent {
  uint32_t size;
  bool littleEndian;
};

// Declares the actual readable bytes of a native indexed array. The owner must
// retain both these bytes and the registration until commands borrowing them
// have been consumed. Registration does not make mutable source data immutable.
// Identical registrations may share ownership; conflicting overlaps are errors.
class ArrayRegistration final {
public:
  ArrayRegistration() noexcept = default;
  ArrayRegistration(const void* data, size_t size, bool littleEndian);
  ~ArrayRegistration();
  ArrayRegistration(ArrayRegistration&& other) noexcept;
  ArrayRegistration& operator=(ArrayRegistration&& other) noexcept;
  ArrayRegistration(const ArrayRegistration&) = delete;
  ArrayRegistration& operator=(const ArrayRegistration&) = delete;

  void reset() noexcept;

private:
  std::shared_ptr<detail::ArrayRegistryState> state_;
  uintptr_t base_ = 0;
};

// Interior pointers receive only the remaining bytes. An unregistered pointer
// has no inferred extent; in particular this never widens an allocation guess.
std::optional<ArrayExtent> find_registered_array(const void* data);

} // namespace aurora::gx
