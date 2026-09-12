#pragma once

#include "frame_limits.hpp"
#include <array>
#include <atomic>
#include <cstdint>
#include <stdexcept>

namespace aurora::gfx {

static_assert(std::atomic<uint64_t>::is_always_lock_free,
              "GX abort requires lock-free interrupt-visible command state");
inline std::atomic<uint64_t> g_commandEpoch{1};
struct CommandEpoch {
  uint64_t value = g_commandEpoch.load(std::memory_order_acquire);
  [[nodiscard]] bool current() const noexcept {
    return value == g_commandEpoch.load(std::memory_order_acquire);
  }
};

namespace detail {
enum class SubmissionOutcome : uint64_t { Pending = 1, Committed = 2, Abandoned = 3 };
struct SubmissionSlot {
  std::atomic<uint64_t> identity{0};
  std::atomic<uint64_t> epoch{0};
};
inline std::array<SubmissionSlot, SubmissionSlotCount> g_submissionSlots;
inline std::atomic<uint64_t> g_nextSubmission{1};
}

// The abort interrupt never dereferences an encoder-owned object. Stable slots
// arbitrate submission vs abandonment with one CAS, and markers publish their
// final outcome before releasing a slot. Retained copy history uses no slots.
struct SubmissionState {
  CommandEpoch epoch;

  SubmissionState() {
    using namespace detail;
    identity_ = g_nextSubmission.fetch_add(1, std::memory_order_relaxed) << 2;
    for (auto& slot : g_submissionSlots) {
      uint64_t empty = 0;
      // 1 reserves storage but has no published identity. An abort racing this
      // setup is handled by the epoch recheck before construction returns.
      if (!slot.identity.compare_exchange_strong(empty, 1, std::memory_order_acq_rel)) continue;
      slot_ = &slot;
      slot.epoch.store(epoch.value, std::memory_order_relaxed);
      slot.identity.store(identity_ | uint64_t(SubmissionOutcome::Pending), std::memory_order_release);
      if (!epoch.current()) retire();
      return;
    }
    throw std::runtime_error("GX submission ownership exceeds its bounded frame/encoder slots");
  }
  ~SubmissionState() { retire(); }
  SubmissionState(const SubmissionState&) = delete;
  SubmissionState& operator=(const SubmissionState&) = delete;

  [[nodiscard]] bool commit() noexcept {
    using namespace detail;
    if (outcome() != SubmissionOutcome::Pending) return is_submitted();
    uint64_t pending = identity_ | uint64_t(SubmissionOutcome::Pending);
    slot_->identity.compare_exchange_strong(pending, identity_ | uint64_t(SubmissionOutcome::Committed),
                                            std::memory_order_acq_rel);
    finalize();
    return is_submitted();
  }
  void retire() noexcept {
    using namespace detail;
    if (final_.load(std::memory_order_acquire) != SubmissionOutcome::Pending) return;
    uint64_t pending = identity_ | uint64_t(SubmissionOutcome::Pending);
    slot_->identity.compare_exchange_strong(pending, identity_ | uint64_t(SubmissionOutcome::Abandoned),
                                            std::memory_order_acq_rel);
    finalize();
  }
  [[nodiscard]] bool is_submitted() const noexcept {
    return outcome() == detail::SubmissionOutcome::Committed;
  }
  [[nodiscard]] bool abandoned() const noexcept {
    return outcome() == detail::SubmissionOutcome::Abandoned;
  }
private:
  detail::SubmissionOutcome outcome() const noexcept {
    using namespace detail;
    const auto final = final_.load(std::memory_order_acquire);
    if (final != SubmissionOutcome::Pending) return final;
    const auto current = slot_->identity.load(std::memory_order_acquire);
    if ((current & ~uint64_t(3)) == identity_) return SubmissionOutcome(current & 3);
    // Reuse follows publication of this marker's final outcome.
    return final_.load(std::memory_order_acquire);
  }
  void finalize() noexcept {
    const auto value = outcome();
    final_.store(value, std::memory_order_release);
    slot_->identity.store(0, std::memory_order_release);
  }
  detail::SubmissionSlot* slot_ = nullptr;
  uint64_t identity_ = 0;
  std::atomic<detail::SubmissionOutcome> final_{detail::SubmissionOutcome::Pending};
};

inline void abandon_command_epoch() noexcept {
  using namespace detail;
  const auto abandonedEpoch = g_commandEpoch.fetch_add(1, std::memory_order_acq_rel);
  for (auto& slot : g_submissionSlots) {
    auto value = slot.identity.load(std::memory_order_acquire);
    if (value <= 1 || (value & 3) != uint64_t(SubmissionOutcome::Pending) ||
        slot.epoch.load(std::memory_order_acquire) > abandonedEpoch) continue;
    slot.identity.compare_exchange_strong(value, (value & ~uint64_t(3)) | uint64_t(SubmissionOutcome::Abandoned),
                                           std::memory_order_acq_rel);
  }
}

} // namespace aurora::gfx
