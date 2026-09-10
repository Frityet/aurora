#include <aurora/gx_array.hpp>

#include <aurora/allocation.hpp>
#include <aurora/exception.hpp>

#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace aurora::gx {
namespace detail {
struct ArrayRegistryState {
  struct Entry {
    ArrayExtent extent;
    size_t owners = 1;
  };
  std::mutex mutex;
  std::map<uintptr_t, Entry> entries;
};
} // namespace detail

namespace {
std::shared_ptr<detail::ArrayRegistryState> registry_state() {
  const allocation::HostAllocationScope host;
  static const auto state = std::make_shared<detail::ArrayRegistryState>();
  return state;
}
} // namespace

ArrayRegistration::ArrayRegistration(const void* data, size_t size, bool littleEndian) {
  const allocation::HostAllocationScope host;
  const auto base = reinterpret_cast<uintptr_t>(data);
  if (data == nullptr || size == 0 || size > std::numeric_limits<uint32_t>::max() ||
      size > std::numeric_limits<uintptr_t>::max() - base) {
    throw_host_exception<std::invalid_argument>("Invalid native GX array extent");
  }
  auto state = registry_state();
  std::lock_guard lock(state->mutex);
  auto next = state->entries.lower_bound(base);
  if (next != state->entries.end() && next->first == base && next->second.extent.size == size &&
      next->second.extent.littleEndian == littleEndian) {
    ++next->second.owners;
  } else {
    const bool overlapsNext = next != state->entries.end() && next->first < base + size;
    const bool overlapsPrevious = next != state->entries.begin() &&
                                  std::prev(next)->first + std::prev(next)->second.extent.size > base;
    if (overlapsNext || overlapsPrevious) {
      throw_host_exception<std::invalid_argument>("Conflicting native GX array extents overlap");
    }
    state->entries.emplace_hint(next, base, detail::ArrayRegistryState::Entry{{static_cast<uint32_t>(size), littleEndian}});
  }
  base_ = base;
  state_ = std::move(state);
}

ArrayRegistration::~ArrayRegistration() { reset(); }

ArrayRegistration::ArrayRegistration(ArrayRegistration&& other) noexcept
: state_(std::move(other.state_)), base_(std::exchange(other.base_, 0)) {}

ArrayRegistration& ArrayRegistration::operator=(ArrayRegistration&& other) noexcept {
  if (this != &other) {
    reset();
    state_ = std::move(other.state_);
    base_ = std::exchange(other.base_, 0);
  }
  return *this;
}

void ArrayRegistration::reset() noexcept {
  const allocation::HostAllocationScope host;
  if (!state_) return;
  // Keep the registry alive independently of static destruction order.
  auto state = std::move(state_);
  std::lock_guard lock(state->mutex);
  auto entry = state->entries.find(base_);
  if (--entry->second.owners == 0) state->entries.erase(entry);
  base_ = 0;
}

std::optional<ArrayExtent> find_registered_array(const void* data) {
  const auto state = registry_state();
  std::lock_guard lock(state->mutex);
  const auto address = reinterpret_cast<uintptr_t>(data);
  auto entry = state->entries.upper_bound(address);
  if (entry == state->entries.begin()) return std::nullopt;
  --entry;
  const auto offset = address - entry->first;
  if (offset >= entry->second.extent.size) return std::nullopt;
  return ArrayExtent{static_cast<uint32_t>(entry->second.extent.size - offset), entry->second.extent.littleEndian};
}

} // namespace aurora::gx
