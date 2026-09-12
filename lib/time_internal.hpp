#pragma once

#include <aurora/time.hpp>

#include <cstdint>

namespace aurora::time::internal {

enum class PauseReason : uint32_t {
  Window = 1u << 0,
  Surface = 1u << 1,
  Background = 1u << 2,
};

void set_pause_reason(PauseReason reason, bool paused) noexcept;

using NowFunction = native_clock::time_point (*)() noexcept;

// Effective rate includes both explicit scale and independent pause reasons.
float effective_scale() noexcept;

// A native deadline waiter uses this to wake immediately on clock changes.
// Notifications run without the clock-state lock and serialize with listener
// retirement. The callback must not change the clock or register listeners;
// construction/destruction must not hold locks taken by the callback.
struct ClockChangeListeners;
class ClockChangeListener final {
public:
  using Notify = void (*)(void*) noexcept;
  ClockChangeListener(Notify notify, void* context);
  ~ClockChangeListener();
  ClockChangeListener(const ClockChangeListener&) = delete;
  ClockChangeListener& operator=(const ClockChangeListener&) = delete;

private:
  friend struct ClockChangeListeners;
  Notify notify_;
  void* context_;
  ClockChangeListener* next_ = nullptr;
};

// Test hooks
void set_now_function(NowFunction function) noexcept;
void reset() noexcept;

} // namespace aurora::time::internal
