#include <dolphin/os.h>
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <aurora/time.hpp>
#include "time_internal.hpp"
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

struct Alarm {
  OSAlarm value{};
  Alarm() { OSCreateAlarm(&value); }
  ~Alarm() { OSCancelAlarm(&value); }
  Alarm(const Alarm&) = delete;
  Alarm& operator=(const Alarm&) = delete;
};

class OSAlarmTest : public testing::Test {
  void SetUp() override { aurora::time::set_scale(1); }
  void TearDown() override {
    aurora::time::set_scale(1);
    aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, false);
    EXPECT_TRUE(OSCheckAlarmQueue());
  }
};

TEST_F(OSAlarmTest, CreatePreservesUserDataAndOneShotUsesRealInterruptContext) {
  struct State {
    OSContext* expectedContext;
    OSThread* expectedThread;
    std::promise<void> done;
  } state{OSGetCurrentContext(), OSGetCurrentThread(), {}};
  OSAlarm alarm{};
  alarm.userData = &state;
  alarm.tag = 45;
  OSCreateAlarm(&alarm);
  EXPECT_EQ(alarm.userData, &state);
  EXPECT_EQ(alarm.tag, 0);
  auto done = state.done.get_future();
  const auto routing = aurora::allocation::routing_state;
  aurora::allocation::routing_state = {false, true};
  OSSetAlarm(&alarm, OSMillisecondsToTicks(10), [](OSAlarm* alarm, OSContext* interrupted) {
    auto& state = *static_cast<State*>(OSGetAlarmUserData(alarm));
    EXPECT_EQ(alarm->handler, nullptr);
    EXPECT_EQ(interrupted, state.expectedContext);
    EXPECT_EQ(OSGetCurrentThread(), state.expectedThread);
    EXPECT_NE(OSGetCurrentContext(), interrupted);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(OSDisableScheduler(), 1);
    EXPECT_EQ(OSEnableScheduler(), 2);
    EXPECT_TRUE(aurora::allocation::routing_state.guest);
    state.done.set_value();
  });
  aurora::allocation::routing_state = routing;
  EXPECT_EQ(done.wait_for(2s), std::future_status::ready);
  OSCancelAlarm(&alarm);
  EXPECT_EQ(OSGetCurrentContext(), state.expectedContext);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  OSRestoreInterrupts(TRUE);
}

TEST_F(OSAlarmTest, EqualDeadlinesRemainFifoAndCancelAllowsImmediateReuse) {
  struct Item { std::vector<int>* order; int value; std::promise<void>* done; };
  std::vector<int> order;
  std::promise<void> completed;
  auto done = completed.get_future();
  std::array<Item, 4> items{{{&order, 1, nullptr}, {&order, 2, nullptr},
                           {&order, 3, nullptr}, {&order, 4, &completed}}};
  std::array<Alarm, 4> alarms;
  aurora::time::set_scale(0);
  const auto deadline = OSGetTime() + OSMillisecondsToTicks(10);
  auto handler = [](OSAlarm* alarm, OSContext*) {
    auto& item = *static_cast<Item*>(OSGetAlarmUserData(alarm));
    item.order->push_back(item.value);
    if (item.done != nullptr) item.done->set_value();
  };
  for (int i = 0; i < 4; ++i) {
    OSSetAlarmUserData(&alarms[i].value, &items[i]);
    OSSetAbsAlarm(&alarms[i].value, deadline, handler);
  }
  OSCancelAlarm(&alarms[1].value);
  OSCreateAlarm(&alarms[1].value);
  EXPECT_EQ(alarms[1].value.userData, &items[1]);
  // Put the reused address before the final completion marker.
  OSCancelAlarm(&alarms[3].value);
  OSSetAbsAlarm(&alarms[1].value, deadline, handler);
  OSSetAbsAlarm(&alarms[3].value, deadline, handler);
  EXPECT_TRUE(OSCheckAlarmQueue());
  aurora::time::set_scale(1);
  EXPECT_EQ(done.wait_for(2s), std::future_status::ready);
  for (auto& alarm : alarms) OSCancelAlarm(&alarm.value);
  EXPECT_EQ(order, (std::vector<int>{1, 3, 2, 4}));
}

TEST_F(OSAlarmTest, CancelSynchronizesWithCallbackAndTagCancellationPreservesOthers) {
  struct State { std::promise<void> entered; std::shared_future<void> release; };
  std::promise<void> release;
  State state{{}, release.get_future().share()};
  auto entered = state.entered.get_future();
  Alarm alarm;
  OSSetAlarmUserData(&alarm.value, &state);
  OSSetAlarm(&alarm.value, 0, [](OSAlarm* alarm, OSContext*) {
    auto& state = *static_cast<State*>(OSGetAlarmUserData(alarm));
    state.entered.set_value();
    state.release.wait(); // Test barrier only; no blocking SDK operation in ISR.
  });
  const auto status = entered.wait_for(2s);
  if (status != std::future_status::ready) release.set_value();
  ASSERT_EQ(status, std::future_status::ready);
  auto cancel = std::async(std::launch::async, [&] { OSCancelAlarm(&alarm.value); });
  EXPECT_EQ(cancel.wait_for(20ms), std::future_status::timeout);
  release.set_value();
  EXPECT_EQ(cancel.wait_for(2s), std::future_status::ready);
  cancel.get();
  Alarm first, second, retained;
  auto handler = [](OSAlarm*, OSContext*) { ADD_FAILURE() << "Canceled/future alarm fired"; };
  for (auto* value : {&first.value, &second.value, &retained.value}) {
    OSSetAlarmTag(value, value == &retained.value ? 2 : 1);
    OSSetAlarm(value, OSSecondsToTicks(60LL), handler);
  }
  OSCancelAlarms(1);
  EXPECT_EQ(first.value.handler, nullptr);
  EXPECT_EQ(second.value.handler, nullptr);
  EXPECT_NE(retained.value.handler, nullptr);
}


TEST_F(OSAlarmTest, CancelDestroysQueuedStorageBeforeInterruptCanAcquireCpu) {
  std::promise<void> completed;
  auto done = completed.get_future();
  Alarm marker;
  {
    const aurora::os::GuestThreadExecutionScope execution;
    auto removed = std::make_unique<Alarm>();
    OSSetAlarm(&removed->value, 0, [](OSAlarm*, OSContext*) {
      ADD_FAILURE() << "Retired alarm was dispatched";
    });
    std::this_thread::sleep_for(20ms);
    removed.reset();
    OSSetAlarmUserData(&marker.value, &completed);
    OSSetAlarm(&marker.value, 0, [](OSAlarm* alarm, OSContext*) {
      static_cast<std::promise<void>*>(OSGetAlarmUserData(alarm))->set_value();
    });
  }
  EXPECT_EQ(done.wait_for(2s), std::future_status::ready);
}

TEST_F(OSAlarmTest, InterruptWakeDefersReadyGuestUntilCallbackReturns) {
  struct State {
    OSMessageQueue queue{};
    OSMessage storage{};
    bool callbackReturned = false;
    bool observedReturn = false;
  } state;
  OSInitMessageQueue(&state.queue, &state.storage, 1);
  OSThread thread{};
  std::array<unsigned char, 16384> stack{};
  ASSERT_TRUE(OSCreateThread(&thread, [](void* data) -> void* {
    auto& state = *static_cast<State*>(data);
    OSReceiveMessage(&state.queue, nullptr, OS_MESSAGE_BLOCK);
    state.observedReturn = state.callbackReturned;
    return nullptr;
  }, &state, stack.data() + stack.size(), stack.size(), 5, 0));
  OSResumeThread(&thread);
  Alarm alarm;
  OSSetAlarmUserData(&alarm.value, &state);
  OSSetAlarm(&alarm.value, 0, [](OSAlarm* alarm, OSContext*) {
    auto& state = *static_cast<State*>(OSGetAlarmUserData(alarm));
    EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
    EXPECT_FALSE(state.observedReturn);
    state.callbackReturned = true;
  });
  EXPECT_TRUE(OSJoinThread(&thread, nullptr));
  EXPECT_TRUE(state.observedReturn);
}

TEST_F(OSAlarmTest, PeriodicAlarmSkipsMissedPeriodsAndReinsertsBeforeSelfCancel) {
  struct State { OSTime start; OSTime period; OSTime firstFire; std::promise<void> done; };
  State state{OSGetTime() - OSMillisecondsToTicks(105), OSMillisecondsToTicks(20), 0, {}};
  auto done = state.done.get_future();
  Alarm alarm;
  OSSetAlarmUserData(&alarm.value, &state);
  const BOOL interrupts = OSDisableInterrupts();
  OSSetPeriodicAlarm(&alarm.value, state.start, state.period, [](OSAlarm* alarm, OSContext*) {
    auto& state = *static_cast<State*>(OSGetAlarmUserData(alarm));
    EXPECT_NE(alarm->handler, nullptr);
    EXPECT_EQ((alarm->fire - state.start) % state.period, 0);
    EXPECT_GT(alarm->fire, state.firstFire);
    OSCancelAlarm(alarm);
    EXPECT_EQ(alarm->handler, nullptr);
    EXPECT_TRUE(OSCheckAlarmQueue());
    state.done.set_value();
  });
  state.firstFire = alarm.value.fire;
  OSRestoreInterrupts(interrupts);
  EXPECT_EQ(done.wait_for(2s), std::future_status::ready);
}

TEST_F(OSAlarmTest, PausedClockAndScaleChangeWakeDeadlineWaiter) {
  Alarm alarm;
  std::promise<void> completed;
  auto done = completed.get_future();
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, true);
  OSSetAlarmUserData(&alarm.value, &completed);
  OSSetAlarm(&alarm.value, OSMillisecondsToTicks(400), [](OSAlarm* alarm, OSContext*) {
    static_cast<std::promise<void>*>(OSGetAlarmUserData(alarm))->set_value();
  });
  EXPECT_EQ(done.wait_for(30ms), std::future_status::timeout);
  aurora::time::set_scale(16);
  aurora::time::internal::set_pause_reason(aurora::time::internal::PauseReason::Window, false);
  EXPECT_EQ(done.wait_for(300ms), std::future_status::ready);
  OSCancelAlarm(&alarm.value);
  aurora::time::set_scale(1);
  std::promise<void> accelerated;
  auto fast = accelerated.get_future();
  OSSetAlarmUserData(&alarm.value, &accelerated);
  OSSetAlarm(&alarm.value, OSMillisecondsToTicks(800), [](OSAlarm* alarm, OSContext*) {
    static_cast<std::promise<void>*>(OSGetAlarmUserData(alarm))->set_value();
  });
  EXPECT_EQ(fast.wait_for(20ms), std::future_status::timeout);
  aurora::time::set_scale(16);
  EXPECT_EQ(fast.wait_for(300ms), std::future_status::ready);
}

struct SleepingThread {
  OSThread thread{};
  std::array<unsigned char, 16384> stack{};
  std::promise<void> entered;
  std::promise<void> returned;
  OSTime ticks;
  explicit SleepingThread(OSTime ticks) : ticks(ticks) {
    EXPECT_TRUE(OSCreateThread(&thread, [](void* data) -> void* {
      auto& owner = *static_cast<SleepingThread*>(data);
      owner.entered.set_value();
      const auto start = OSGetTime();
      OSSleepTicks(owner.ticks);
      const auto elapsed = OSGetTime() - start;
      owner.returned.set_value();
      return reinterpret_cast<void*>(static_cast<std::uintptr_t>(elapsed));
    }, this, stack.data() + stack.size(), stack.size(), 10, 0));
  }
  ~SleepingThread() {
    if (!OSIsThreadTerminated(&thread)) OSCancelThread(&thread);
    OSJoinThread(&thread, nullptr);
  }
};

TEST_F(OSAlarmTest, SleepTicksUsesOriginalSuspendResumeAndCancelsOnEarlyResume) {
  SleepingThread sleeper{OSMillisecondsToTicks(25)};
  auto returned = sleeper.returned.get_future();
  OSResumeThread(&sleeper.thread);
  EXPECT_EQ(returned.wait_for(2s), std::future_status::ready);
  void* elapsed = nullptr;
  EXPECT_TRUE(OSJoinThread(&sleeper.thread, &elapsed));
  EXPECT_GE(reinterpret_cast<std::uintptr_t>(elapsed), static_cast<u64>(sleeper.ticks));

  SleepingThread early{OSSecondsToTicks(30)};
  auto entered = early.entered.get_future();
  auto resumed = early.returned.get_future();
  OSResumeThread(&early.thread);
  ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);
  // The actual CPU gate prevents inspection before OSSleepTicks suspends.
  EXPECT_TRUE(OSIsThreadSuspended(&early.thread));
  EXPECT_EQ(OSResumeThread(&early.thread), 1);
  EXPECT_EQ(resumed.wait_for(2s), std::future_status::ready);
  EXPECT_TRUE(OSJoinThread(&early.thread, nullptr));
  EXPECT_TRUE(OSCheckAlarmQueue());
}

TEST_F(OSAlarmTest, CancelSleepingThreadUnwindsItsAlarmBeforeStorageRetirement) {
  for (int generation = 0; generation < 4; ++generation) {
    SleepingThread sleeper{OSSecondsToTicks(30)};
    auto entered = sleeper.entered.get_future();
    OSResumeThread(&sleeper.thread);
    ASSERT_EQ(entered.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(OSIsThreadSuspended(&sleeper.thread));
    OSCancelThread(&sleeper.thread);
    EXPECT_TRUE(OSIsThreadTerminated(&sleeper.thread));
    EXPECT_TRUE(OSCheckAlarmQueue());
  }
}
} // namespace
