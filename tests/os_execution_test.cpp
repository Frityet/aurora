#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

TEST(OSExecutionTest, HostWaitReleasesNestedGuestExecutionAndRestoresOwnership) {
  int protectedValue = 0;
  std::future<void> afterWait;
  {
    const aurora::os::GuestThreadExecutionScope outer;
    {
      const aurora::os::GuestThreadExecutionScope inner;
      const BOOL interrupts = OSDisableInterrupts();
      std::promise<void> attempting;
      auto started = attempting.get_future();
      auto callback = std::async(std::launch::async, [&] {
        attempting.set_value();
        const aurora::os::GuestThreadExecutionScope execution;
        protectedValue = 41;
      });
      started.wait();
      EXPECT_EQ(callback.wait_for(20ms), std::future_status::timeout);
      {
        const aurora::os::GuestThreadWaitScope wait;
        const aurora::os::GuestThreadWaitScope nestedWait;
        callback.get();
      }
      EXPECT_EQ(protectedValue, 41);
      EXPECT_EQ(OSDisableInterrupts(), FALSE);
      OSRestoreInterrupts(interrupts);
    }
    std::promise<void> attempting;
    auto started = attempting.get_future();
    afterWait = std::async(std::launch::async, [&] {
      attempting.set_value();
      const aurora::os::GuestThreadExecutionScope execution;
      ++protectedValue;
    });
    started.wait();
    EXPECT_EQ(afterWait.wait_for(20ms), std::future_status::timeout);
    EXPECT_EQ(protectedValue, 41);
  }
  afterWait.get();
  EXPECT_EQ(protectedValue, 42);
}

TEST(OSExecutionTest, HostWaitWithoutCpuOwnershipDoesNotAcquireItOnExit) {
  {
    const aurora::os::GuestThreadWaitScope wait;
    const aurora::os::GuestThreadWaitScope nested;
  }
  auto callback = std::async(std::launch::async, [] {
    const aurora::os::GuestThreadExecutionScope execution;
    return 17;
  });
  EXPECT_EQ(callback.get(), 17);
}

TEST(OSExecutionTest, DeferredCallbackRoutingIsCapturedAndRestoredOnTheWorker) {
  const aurora::allocation::RoutingState captured{false, true};
  auto callback = std::async(std::launch::async, [captured] {
    const auto before = aurora::allocation::routing_state;
    bool selected = false;
    {
      const aurora::os::GuestThreadExecutionScope execution;
      const aurora::allocation::ClientAllocationScope allocations{captured};
      selected = aurora::allocation::routing_state.guest && aurora::allocation::routing_state.callbackGuest;
    }
    return selected && aurora::allocation::routing_state.guest == before.guest &&
           aurora::allocation::routing_state.callbackGuest == before.callbackGuest;
  });
  EXPECT_TRUE(callback.get());
}

TEST(OSExecutionTest, InterruptNestingReturnsAndRestoresSavedBits) {
  const BOOL outer = OSDisableInterrupts();
  const BOOL inner = OSDisableInterrupts();
  EXPECT_EQ(outer, TRUE);
  EXPECT_EQ(inner, FALSE);
  EXPECT_EQ(OSRestoreInterrupts(inner), FALSE);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSRestoreInterrupts(outer), FALSE);
  EXPECT_EQ(OSEnableInterrupts(), TRUE);
}

TEST(OSExecutionTest, RestoreSetsBooleanStateRatherThanCountingDisables) {
  EXPECT_EQ(OSRestoreInterrupts(FALSE), TRUE);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSRestoreInterrupts(-7), FALSE);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  EXPECT_EQ(OSEnableInterrupts(), FALSE);
  EXPECT_EQ(OSRestoreInterrupts(19), TRUE);
}

TEST(OSExecutionTest, SchedulerReturnsPriorCountAndDoesNotClampUnderflow) {
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSDisableScheduler(), 1);
  EXPECT_EQ(OSDisableScheduler(), 2);
  EXPECT_EQ(OSEnableScheduler(), 3);
  EXPECT_EQ(OSEnableScheduler(), 2);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(OSEnableScheduler(), 0);
  EXPECT_EQ(OSDisableScheduler(), -1);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSEnableScheduler(), 1);
}

TEST(OSExecutionTest, SchedulerCallsPreserveTheCallersInterruptBit) {
  const BOOL saved = OSDisableInterrupts();
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSRestoreInterrupts(saved), FALSE);
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSDisableInterrupts(), TRUE);
  EXPECT_EQ(OSRestoreInterrupts(TRUE), FALSE);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(OSEnableInterrupts(), TRUE);
}

TEST(OSExecutionTest, InterruptGateExcludesOtherCallersUntilTrueRestore) {
  int protectedValue = 41;
  const BOOL saved = OSDisableInterrupts();
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const BOOL previous = OSDisableInterrupts();
    ++protectedValue;
    OSRestoreInterrupts(previous);
    return previous;
  });
  started.wait();
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(OSRestoreInterrupts(FALSE), FALSE);
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(protectedValue, 41);
  OSRestoreInterrupts(saved);
  EXPECT_EQ(result.get(), TRUE);
  EXPECT_EQ(protectedValue, 42);
}

TEST(OSExecutionTest, SchedulerGateExcludesInterruptCallersAcrossPartialEnable) {
  int protectedValue = 11;
  EXPECT_EQ(OSDisableScheduler(), 0);
  EXPECT_EQ(OSDisableScheduler(), 1);
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const BOOL previous = OSDisableInterrupts();
    protectedValue *= 3;
    OSRestoreInterrupts(previous);
    return previous;
  });
  started.wait();
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(OSEnableScheduler(), 2);
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(protectedValue, 11);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(result.get(), TRUE);
  EXPECT_EQ(protectedValue, 33);
}

TEST(OSExecutionTest, EnabledInterruptsDoNotReleaseADisabledScheduler) {
  const BOOL saved = OSDisableInterrupts();
  EXPECT_EQ(OSDisableScheduler(), 0);
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const s32 previous = OSDisableScheduler();
    OSEnableScheduler();
    return previous;
  });
  started.wait();
  EXPECT_EQ(OSRestoreInterrupts(saved), FALSE);
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(result.get(), 0);
}

TEST(OSExecutionTest, EnabledSchedulerDoesNotReleaseDisabledInterrupts) {
  EXPECT_EQ(OSDisableScheduler(), 0);
  const BOOL saved = OSDisableInterrupts();
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const s32 previous = OSDisableScheduler();
    OSEnableScheduler();
    return previous;
  });
  started.wait();
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  OSRestoreInterrupts(saved);
  EXPECT_EQ(result.get(), 0);
}

TEST(OSExecutionTest, MixedCriticalSectionsProtectSharedMemoryAcrossThreads) {
  constexpr int threadCount = 6;
  constexpr int increments = 2000;
  std::barrier start{threadCount};
  std::atomic<int> wrongPrevious{0};
  int protectedValue = 0;
  std::vector<std::jthread> threads;
  for (int i = 0; i < threadCount; ++i) {
    threads.emplace_back([&, i] {
      start.arrive_and_wait();
      for (int j = 0; j < increments; ++j) {
        if (i % 2 == 0) {
          const BOOL previous = OSDisableInterrupts();
          if (previous != TRUE) {
            ++wrongPrevious;
          }
          ++protectedValue;
          OSRestoreInterrupts(previous);
        } else {
          if (OSDisableScheduler() != 0) {
            ++wrongPrevious;
          }
          ++protectedValue;
          if (OSEnableScheduler() != 1) {
            ++wrongPrevious;
          }
        }
      }
    });
  }
  threads.clear();
  EXPECT_EQ(protectedValue, threadCount * increments);
  EXPECT_EQ(wrongPrevious.load(), 0);
}

TEST(OSExecutionTest, YieldRetainsOwnershipWhileSchedulerIsDisabled) {
  EXPECT_EQ(OSDisableScheduler(), 0);
  const BOOL saved = OSDisableInterrupts();
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const BOOL previous = OSDisableInterrupts();
    OSRestoreInterrupts(previous);
    return previous;
  });
  started.wait();
  for (int i = 0; i < 50; ++i) {
    OSYieldThread();
  }
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(OSEnableScheduler(), 1);
  OSRestoreInterrupts(saved);
  EXPECT_EQ(result.get(), TRUE);
}

TEST(OSExecutionTest, ExplicitYieldReclaimsTheCallersDisabledInterruptState) {
  const BOOL saved = OSDisableInterrupts();
  int protectedValue = 42;
  std::promise<void> attempting;
  auto started = attempting.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempting.set_value();
    const BOOL previous = OSDisableInterrupts();
    protectedValue += 15;
    OSRestoreInterrupts(previous);
    return previous;
  });
  started.wait();
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  do {
    OSYieldThread();
  } while (result.wait_for(0ms) != std::future_status::ready && std::chrono::steady_clock::now() < deadline);
  EXPECT_EQ(result.wait_for(0ms), std::future_status::ready);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  EXPECT_EQ(protectedValue, 57);
  OSRestoreInterrupts(saved);
  EXPECT_EQ(result.get(), TRUE);
  EXPECT_EQ(OSEnableInterrupts(), TRUE);
}

TEST(OSExecutionTest, YieldPreservesEnabledInterruptsAndAllowsNegativeCount) {
  OSYieldThread();
  EXPECT_EQ(OSEnableInterrupts(), TRUE);
  EXPECT_EQ(OSEnableScheduler(), 0);
  OSYieldThread();
  EXPECT_EQ(OSEnableInterrupts(), TRUE);
  EXPECT_EQ(OSDisableScheduler(), -1);
}

} // namespace

namespace {
struct ManagedTestThread {
  OSThread thread{};
  alignas(32) std::byte stack[4096]{};

  ManagedTestThread(void* (*entry)(void*), void* argument, OSPriority priority) {
    if (!OSCreateThread(&thread, entry, argument, stack + sizeof(stack), sizeof(stack), priority, 0))
      std::abort();
  }
  ~ManagedTestThread() { OSCancelThread(&thread); }
  ManagedTestThread(const ManagedTestThread&) = delete;
};

struct QueueWorker {
  OSMessageQueue queue{};
  OSMessage slot{};
  int steps = 0;
  OSMessage received = nullptr;
  static void* run(void* argument) {
    auto& state = *static_cast<QueueWorker*>(argument);
    OSInitMessageQueue(&state.queue, &state.slot, 1);
    ++state.steps;
    OSReceiveMessage(&state.queue, &state.received, OS_MESSAGE_BLOCK);
    ++state.steps;
    return state.received;
  }
};

TEST(OSExecutionTest, HigherPriorityResumeInitializesQueueBeforeCallerContinues) {
  const aurora::os::GuestThreadExecutionScope execution;
  for (int cycle = 0; cycle < 64; ++cycle) {
    QueueWorker state;
    ManagedTestThread worker(QueueWorker::run, &state, 8);
    EXPECT_EQ(OSResumeThread(&worker.thread), 1);
    EXPECT_EQ(state.steps, 1);
    EXPECT_EQ(state.queue.msgCount, 1);
    EXPECT_EQ(worker.thread.state, OS_THREAD_STATE_WAITING);
    EXPECT_EQ(state.queue.queueReceive.head, &worker.thread);
    const auto payload = reinterpret_cast<OSMessage>(uintptr_t{0x123456789ABC});
    EXPECT_TRUE(OSSendMessage(&state.queue, payload, OS_MESSAGE_NOBLOCK));
    EXPECT_EQ(state.steps, 2);
    EXPECT_EQ(state.received, payload);
    void* result = nullptr;
    EXPECT_TRUE(OSJoinThread(&worker.thread, &result));
    EXPECT_EQ(result, payload);
  }
}

TEST(OSExecutionTest, PriorityPreemptionPreservesMaskedInterruptState) {
  const aurora::os::GuestThreadExecutionScope execution;
  QueueWorker state;
  ManagedTestThread worker(QueueWorker::run, &state, 7);
  const BOOL saved = OSDisableInterrupts();
  OSResumeThread(&worker.thread);
  EXPECT_EQ(state.steps, 1);
  EXPECT_FALSE(OSDisableInterrupts());
  EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(state.steps, 2);
  EXPECT_FALSE(OSDisableInterrupts());
  OSRestoreInterrupts(saved);
  EXPECT_TRUE(OSJoinThread(&worker.thread, nullptr));
}

TEST(OSExecutionTest, SchedulerDisableDefersPreemptionAndExplicitYieldHonorsPendingPriority) {
  const aurora::os::GuestThreadExecutionScope execution;
  QueueWorker state;
  ManagedTestThread worker(QueueWorker::run, &state, 7);
  OSDisableScheduler();
  OSResumeThread(&worker.thread);
  EXPECT_EQ(state.steps, 0);
  OSYieldThread();
  EXPECT_EQ(state.steps, 0);
  OSEnableScheduler();
  // The SDK's enable call only changes the nesting count. SelectThread is
  // requested by the next scheduling operation, not invented here.
  EXPECT_EQ(state.steps, 0);
  OSYieldThread();
  EXPECT_EQ(state.steps, 1);
  EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSJoinThread(&worker.thread, nullptr));
}

TEST(OSExecutionTest, NestedSuspensionOnlyBecomesRunnableOnFinalResume) {
  const aurora::os::GuestThreadExecutionScope execution;
  QueueWorker state;
  ManagedTestThread worker(QueueWorker::run, &state, 7);
  EXPECT_EQ(OSSuspendThread(&worker.thread), 1);
  EXPECT_EQ(OSResumeThread(&worker.thread), 2);
  EXPECT_EQ(state.steps, 0);
  EXPECT_EQ(OSResumeThread(&worker.thread), 1);
  EXPECT_EQ(state.steps, 1);
  // A suspended wait-queue member may be woken but must not become runnable
  // until resumed. Its real message remains available for that continuation.
  EXPECT_EQ(OSSuspendThread(&worker.thread), 0);
  EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(state.steps, 1);
  EXPECT_EQ(worker.thread.state, OS_THREAD_STATE_READY);
  EXPECT_EQ(worker.thread.queue, nullptr);
  EXPECT_EQ(OSResumeThread(&worker.thread), 1);
  EXPECT_EQ(state.steps, 2);
  EXPECT_TRUE(OSJoinThread(&worker.thread, nullptr));
}

TEST(OSExecutionTest, ReturningFromNativeWaitHonorsReadyGuestPriority) {
  const aurora::os::GuestThreadExecutionScope execution;
  QueueWorker state;
  ManagedTestThread worker(QueueWorker::run, &state, 7);
  OSDisableScheduler();
  OSResumeThread(&worker.thread);
  OSEnableScheduler();
  EXPECT_EQ(state.steps, 0);
  {
    const aurora::os::GuestThreadWaitScope wait;
  }
  EXPECT_EQ(state.steps, 1);
  EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSJoinThread(&worker.thread, nullptr));
}

TEST(OSExecutionTest, EqualPriorityYieldUsesReadyOrderAndNeverRunsLowerPriorityFirst) {
  const aurora::os::GuestThreadExecutionScope execution;
  struct Record {
    std::vector<int>* order;
    int value;
    static void* run(void* argument) {
      auto& record = *static_cast<Record*>(argument);
      record.order->push_back(record.value);
      return nullptr;
    }
  };
  std::vector<int> order;
  Record low{&order, 31}, first{&order, 1}, second{&order, 2};
  ManagedTestThread lowWorker(Record::run, &low, 31);
  ManagedTestThread firstWorker(Record::run, &first, 16);
  ManagedTestThread secondWorker(Record::run, &second, 16);
  OSResumeThread(&lowWorker.thread);
  OSResumeThread(&firstWorker.thread);
  OSResumeThread(&secondWorker.thread);
  EXPECT_TRUE(order.empty());
  OSYieldThread();
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
  EXPECT_TRUE(OSJoinThread(&firstWorker.thread, nullptr));
  EXPECT_TRUE(OSJoinThread(&secondWorker.thread, nullptr));
  EXPECT_TRUE(OSJoinThread(&lowWorker.thread, nullptr));
  EXPECT_EQ(order, (std::vector<int>{1, 2, 31}));
}

TEST(OSExecutionTest, PriorityChangeReordersReadyThreadsBeforeReturning) {
  const aurora::os::GuestThreadExecutionScope execution;
  QueueWorker state;
  ManagedTestThread worker(QueueWorker::run, &state, 24);
  OSResumeThread(&worker.thread);
  EXPECT_EQ(state.steps, 0);
  EXPECT_TRUE(OSSetThreadPriority(&worker.thread, 7));
  EXPECT_EQ(state.steps, 1);
  EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSJoinThread(&worker.thread, nullptr));
}

TEST(OSExecutionTest, CancellingReadyThreadRemovesItsSchedulerLink) {
  const aurora::os::GuestThreadExecutionScope execution;
  for (int cycle = 0; cycle < 32; ++cycle) {
    QueueWorker state;
    ManagedTestThread cancelled(QueueWorker::run, &state, 24);
    OSResumeThread(&cancelled.thread);
    EXPECT_EQ(state.steps, 0);
    OSCancelThread(&cancelled.thread);
    EXPECT_EQ(state.steps, 0);
    EXPECT_TRUE(OSIsThreadTerminated(&cancelled.thread));
    EXPECT_EQ(cancelled.thread.queue, nullptr);
    ManagedTestThread next(QueueWorker::run, &state, 7);
    OSResumeThread(&next.thread);
    EXPECT_EQ(state.steps, 1);
    EXPECT_TRUE(OSSendMessage(&state.queue, nullptr, OS_MESSAGE_NOBLOCK));
    EXPECT_TRUE(OSJoinThread(&next.thread, nullptr));
  }
}
} // namespace
