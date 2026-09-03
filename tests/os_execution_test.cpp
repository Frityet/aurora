#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>
#include <gtest/gtest.h>

#include <atomic>
#include <barrier>
#include <chrono>
#include <future>
#include <thread>
#include <vector>

namespace {

using namespace std::chrono_literals;

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
