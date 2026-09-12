#include <aurora/time.hpp>

#include "time_internal.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>
#include <shared_mutex>

namespace aurora::time {
namespace internal {
struct ClockChangeListeners {
  std::mutex mutex;
  ClockChangeListener* head = nullptr;

  static ClockChangeListeners& get() {
    static ClockChangeListeners listeners;
    return listeners;
  }

  static void notify() noexcept {
    auto& listeners = get();
    std::lock_guard lock{listeners.mutex};
    for (auto* item = listeners.head; item != nullptr; item = item->next_) {
      item->notify_(item->context_);
    }
  }
};

ClockChangeListener::ClockChangeListener(Notify notify, void* context) : notify_(notify), context_(context) {
  auto& listeners = ClockChangeListeners::get();
  std::lock_guard lock{listeners.mutex};
  next_ = listeners.head;
  listeners.head = this;
}

ClockChangeListener::~ClockChangeListener() {
  auto& listeners = ClockChangeListeners::get();
  std::lock_guard lock{listeners.mutex};
  auto** link = &listeners.head;
  while (*link != this) link = &(*link)->next_;
  *link = next_;
}
} // namespace internal

namespace {

native_clock::time_point default_native_now() noexcept {
  const auto elapsed = std::chrono::steady_clock::now().time_since_epoch();
  return native_clock::time_point{std::chrono::duration_cast<native_clock::duration>(elapsed)};
}

std::atomic<internal::NowFunction> s_nativeNow{default_native_now};

struct ClockState {
  std::shared_mutex mutex;
  native_clock::time_point nativeAnchor = default_native_now();
  game_clock::time_point gameAnchor;
  double requestedScale = 1.0;
  uint32_t pauseReasons = 0;
};

ClockState& state() {
  static ClockState clockState;
  return clockState;
}

native_clock::time_point get_native_now() noexcept { return s_nativeNow.load(std::memory_order_acquire)(); }

game_clock::time_point game_now_locked(const ClockState& clockState,
                                       const native_clock::time_point nativeNow) noexcept {
  if (clockState.pauseReasons != 0 || clockState.requestedScale == 0.0) {
    return clockState.gameAnchor;
  }

  return clockState.gameAnchor +
         std::chrono::duration_cast<game_clock::duration>(std::chrono::duration<double, std::nano>{
             (nativeNow - clockState.nativeAnchor).count() * clockState.requestedScale});
}

void rebase_locked(ClockState& clockState, const native_clock::time_point nativeNow) noexcept {
  clockState.gameAnchor = game_now_locked(clockState, nativeNow);
  clockState.nativeAnchor = nativeNow;
}

} // namespace

native_clock::time_point native_clock::now() noexcept { return get_native_now(); }

game_clock::time_point game_clock::now() noexcept {
  auto& clockState = state();
  std::shared_lock lock{clockState.mutex};
  return game_now_locked(clockState, get_native_now());
}

void set_scale(const float scale) noexcept {
  if (!std::isfinite(scale) || scale < 0.0f) {
    return;
  }

  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  const double clampedScale = std::min(static_cast<double>(scale), static_cast<double>(kMaximumTimeScale));
  if (clockState.requestedScale == clampedScale) {
    return;
  }
  const auto nativeNow = get_native_now();
  rebase_locked(clockState, nativeNow);
  clockState.requestedScale = clampedScale;
  lock.unlock();
  internal::ClockChangeListeners::notify();
}

float scale() noexcept {
  auto& clockState = state();
  std::shared_lock lock{clockState.mutex};
  return static_cast<float>(clockState.requestedScale);
}

namespace internal {

float effective_scale() noexcept {
  auto& clockState = state();
  std::shared_lock lock{clockState.mutex};
  return clockState.pauseReasons == 0 ? static_cast<float>(clockState.requestedScale) : 0.0f;
}

void set_pause_reason(const PauseReason reason, const bool paused) noexcept {
  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  const auto mask = static_cast<uint32_t>(reason);
  if (((clockState.pauseReasons & mask) != 0) == paused) {
    return;
  }

  const auto nativeNow = get_native_now();
  rebase_locked(clockState, nativeNow);
  if (paused) {
    clockState.pauseReasons |= mask;
  } else {
    clockState.pauseReasons &= ~mask;
  }
  lock.unlock();
  ClockChangeListeners::notify();
}

void set_now_function(const NowFunction function) noexcept {
  s_nativeNow.store(function != nullptr ? function : default_native_now, std::memory_order_release);
  reset();
}

void reset() noexcept {
  auto& clockState = state();
  std::unique_lock lock{clockState.mutex};
  clockState.nativeAnchor = get_native_now();
  clockState.gameAnchor = {};
  clockState.requestedScale = 1.0;
  clockState.pauseReasons = 0;
  lock.unlock();
  ClockChangeListeners::notify();
}

} // namespace internal
} // namespace aurora::time
