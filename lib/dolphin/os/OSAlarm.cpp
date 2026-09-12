#include <dolphin/os.h>

#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>

#include "../../time_internal.hpp"

#include <algorithm>
#include <bit>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace {
[[noreturn]] void invalid_alarm(const char* reason) {
  std::fprintf(stderr, "Aurora OS alarm: %s\n", reason);
  std::abort();
}

class InterruptMask final {
public:
  InterruptMask() : previous_(OSDisableInterrupts()) {}
  ~InterruptMask() { OSRestoreInterrupts(previous_); }
private:
  BOOL previous_;
};

OSTime add_ticks(OSTime left, OSTime right) {
  return std::bit_cast<OSTime>(std::bit_cast<u64>(left) + std::bit_cast<u64>(right));
}

OSTime periodic_deadline(OSTime start, OSTime period, OSTime now) {
  if (start >= now) return start;
  // The unsigned difference represents the full nonnegative distance, even
  // when start and now straddle zero. Advance to the next phase without signed
  // subtraction/multiplication overflow or compiler-specific integer types.
  const u64 elapsed = std::bit_cast<u64>(now) - std::bit_cast<u64>(start);
  const OSTime advance = period - static_cast<OSTime>(elapsed % static_cast<u64>(period));
  if (now > std::numeric_limits<OSTime>::max() - advance) invalid_alarm("periodic deadline overflow");
  return now + advance;
}

class AlarmService final {
public:
  AlarmService() : clockChanges_(&clock_changed, this), worker_([this] { run(); }) {}
  ~AlarmService() {
    {
      std::lock_guard lock{mutex_};
      stopping_ = true;
      changed_.notify_all();
    }
    // A caller can finish the native process while still holding its ordinary
    // guest execution scope. Permit an already-dispatched interrupt to retire.
    const aurora::os::GuestThreadWaitScope wait;
    worker_.join();
  }

  void create(OSAlarm* alarm) {
    std::lock_guard lock{mutex_};
    if (routing_.contains(alarm)) invalid_alarm("creating an alarm that is still scheduled");
    alarm->handler = nullptr;
    alarm->tag = 0;
  }

  void set(OSAlarm* alarm, OSTime fire, OSTime period, OSAlarmHandler handler,
           aurora::allocation::RoutingState routing) {
    if (handler == nullptr) invalid_alarm("null handler");
    const aurora::allocation::HostAllocationScope host;
    std::lock_guard lock{mutex_};
    if (routing_.contains(alarm)) invalid_alarm("setting an alarm that is still scheduled");
    routing_.emplace(alarm, routing);
    alarm->handler = handler;
    alarm->period = period;
    if (period > 0) {
      alarm->start = fire;
      fire = periodic_deadline(fire, period, OSGetTime());
    }
    insert(alarm, fire);
    changed_.notify_all();
  }

  void cancel(OSAlarm* alarm) {
    const aurora::allocation::HostAllocationScope host;
    std::lock_guard lock{mutex_};
    if (routing_.erase(alarm) == 0) return;
    remove(alarm);
    alarm->handler = nullptr;
    changed_.notify_all();
  }

  void cancel_tag(u32 tag) {
    const aurora::allocation::HostAllocationScope host;
    std::lock_guard lock{mutex_};
    for (auto* alarm = head_; alarm != nullptr;) {
      auto* next = alarm->next;
      if (alarm->tag == tag) {
        remove(alarm);
        routing_.erase(alarm);
        alarm->handler = nullptr;
      }
      alarm = next;
    }
    changed_.notify_all();
  }

  BOOL check() {
    std::lock_guard lock{mutex_};
    OSAlarm* previous = nullptr;
    std::size_t count = 0;
    for (auto* alarm = head_; alarm != nullptr; alarm = alarm->next) {
      if (++count > routing_.size() || alarm->prev != previous || alarm->handler == nullptr ||
          !routing_.contains(alarm) || (previous != nullptr && previous->fire > alarm->fire)) return FALSE;
      previous = alarm;
    }
    return previous == tail_ && count == routing_.size();
  }

private:
  // All intrusive field accesses share this mutex. Guest calls additionally
  // own the CPU gate, serializing cancellation with the complete callback.
  std::mutex mutex_;
  std::condition_variable changed_;
  OSAlarm* head_ = nullptr;
  OSAlarm* tail_ = nullptr;
  std::map<OSAlarm*, aurora::allocation::RoutingState> routing_;
  bool stopping_ = false;
  aurora::time::internal::ClockChangeListener clockChanges_;
  std::thread worker_;

  static void clock_changed(void* context) noexcept {
    auto& service = *static_cast<AlarmService*>(context);
    std::lock_guard lock{service.mutex_};
    service.changed_.notify_all();
  }

  void insert(OSAlarm* alarm, OSTime fire) {
    alarm->fire = fire;
    auto* next = head_;
    while (next != nullptr && next->fire <= fire) next = next->next;
    alarm->next = next;
    alarm->prev = next != nullptr ? next->prev : tail_;
    if (alarm->prev != nullptr) alarm->prev->next = alarm;
    else head_ = alarm;
    if (next != nullptr) next->prev = alarm;
    else tail_ = alarm;
  }

  void remove(OSAlarm* alarm) {
    if (alarm->next != nullptr) alarm->next->prev = alarm->prev;
    else tail_ = alarm->prev;
    if (alarm->prev != nullptr) alarm->prev->next = alarm->next;
    else head_ = alarm->next;
  }

  void run() {
    const aurora::allocation::HostAllocationScope host;
    std::unique_lock lock{mutex_};
    while (!stopping_) {
      if (head_ == nullptr) {
        changed_.wait(lock);
        continue;
      }
      const OSTime now = OSGetTime();
      if (head_->fire > now) {
        const float scale = aurora::time::internal::effective_scale();
        if (scale == 0.0f) changed_.wait(lock);
        else {
          const long double ticks = static_cast<long double>(head_->fire) - now;
          // Clamp native wait durations before converting a possibly distant
          // SDK deadline to the host clock's signed duration.
          const long double seconds = ticks / OS_TIMER_CLOCK / scale;
          changed_.wait_for(lock, std::chrono::duration<long double>{std::min(seconds, 86400.0L)});
        }
        continue;
      }

      // Do not carry a borrowed OSAlarm pointer across CPU acquisition: a
      // simultaneous cancel may return and destroy/reuse its storage there.
      lock.unlock();
      {
        const aurora::os::GuestInterruptExecutionScope interrupt;
        lock.lock();
        if (!stopping_ && head_ != nullptr && head_->fire <= OSGetTime()) {
          auto* alarm = head_;
          const auto handler = alarm->handler;
          const auto routing = routing_.at(alarm);
          remove(alarm);
          alarm->handler = nullptr;
          if (alarm->period > 0) {
            alarm->handler = handler;
            insert(alarm, periodic_deadline(alarm->start, alarm->period, OSGetTime()));
          } else routing_.erase(alarm);
          lock.unlock();
          {
            const aurora::allocation::ClientAllocationScope client{routing};
            handler(alarm, interrupt.interrupted_context());
          }
          // A one-shot handler may destroy its alarm. Never access it again.
        } else lock.unlock();
      }
      lock.lock();
    }
  }
};

AlarmService& service() {
  // Construct both clocks before the worker owner, so they outlive its join.
  (void)OSGetTime();
  const aurora::allocation::HostAllocationScope host;
  static AlarmService alarms;
  return alarms;
}

void sleep_alarm_handler(OSAlarm* alarm, OSContext*) {
  OSResumeThread(static_cast<OSThread*>(OSGetAlarmUserData(alarm)));
}
} // namespace

void OSInitAlarm() { const InterruptMask mask; (void)service(); }
BOOL OSCheckAlarmQueue() { const InterruptMask mask; return service().check(); }
void OSCreateAlarm(OSAlarm* alarm) { const InterruptMask mask; service().create(alarm); }
void OSSetAlarm(OSAlarm* alarm, OSTime tick, OSAlarmHandler handler) {
  const auto routing = aurora::allocation::routing_state;
  const InterruptMask mask;
  service().set(alarm, add_ticks(OSGetTime(), tick), 0, handler, routing);
}
void OSSetAbsAlarm(OSAlarm* alarm, OSTime time, OSAlarmHandler handler) {
  const auto routing = aurora::allocation::routing_state;
  const InterruptMask mask;
  service().set(alarm, time, 0, handler, routing);
}
void OSSetPeriodicAlarm(OSAlarm* alarm, OSTime start, OSTime period, OSAlarmHandler handler) {
  if (period <= 0) invalid_alarm("nonpositive periodic interval");
  const auto routing = aurora::allocation::routing_state;
  const InterruptMask mask;
  service().set(alarm, start, period, handler, routing);
}
void OSCancelAlarm(OSAlarm* alarm) { const InterruptMask mask; service().cancel(alarm); }
void OSSetAlarmTag(OSAlarm* alarm, u32 tag) { const InterruptMask mask; alarm->tag = tag; }
void OSCancelAlarms(u32 tag) { const InterruptMask mask; service().cancel_tag(tag); }
void OSSetAlarmUserData(OSAlarm* alarm, void* userData) { const InterruptMask mask; alarm->userData = userData; }
void* OSGetAlarmUserData(const OSAlarm* alarm) { const InterruptMask mask; return alarm->userData; }

void OSSleepTicks(OSTime ticks) {
  const InterruptMask mask;
  auto* thread = OSGetCurrentThread();
  if (thread == nullptr) return;
  OSAlarm alarm;
  OSCreateAlarm(&alarm);
  OSSetAlarmUserData(&alarm, thread);
  // The Wii used a truncated thread address as a cancellation tag. Native
  // cancellation unwinds this actual stack owner before joining its thread;
  // it needs no 32-bit pointer tag or secondary lifetime registry.
  struct CancelOnExit {
    OSAlarm* alarm;
    ~CancelOnExit() { OSCancelAlarm(alarm); }
  } cancel{&alarm};
  OSSetAlarm(&alarm, ticks, sleep_alarm_handler);
  OSSuspendThread(thread);
}
