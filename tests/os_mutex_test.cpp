#include <dolphin/os.h>
#include "../lib/dolphin/os/thread.hpp"
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <limits>
#include <thread>
#include <vector>

namespace {
using namespace std::chrono_literals;

template <typename F> bool observe(F function) {
  const auto enabled = OSDisableInterrupts();
  const bool value = function();
  OSRestoreInterrupts(enabled);
  return value;
}
template <typename F> bool await(F function) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!observe(function)) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::sleep_for(1ms);
  }
  return true;
}

TEST(OSMutexTest, InitializationAndRecursiveOwnerListAreOriginal) {
  OSMutex first{}, second{};
  first.link.next = &second;
  first.link.prev = &second;
  OSInitMutex(&first);
  OSInitMutex(&second);
  EXPECT_EQ(first.queue.head, nullptr);
  EXPECT_EQ(first.queue.tail, nullptr);
  EXPECT_EQ(first.thread, nullptr);
  EXPECT_EQ(first.count, 0);
  EXPECT_EQ(first.link.next, &second); // OSInitMutex does not write these fields.
  EXPECT_EQ(first.link.prev, &second);
  auto* current = OSGetCurrentThread();
  EXPECT_EQ(current->state, OS_THREAD_STATE_RUNNING);
  EXPECT_EQ(current->base, 16);
  EXPECT_EQ(current->queueMutex.head, nullptr);
  OSLockMutex(&first);
  EXPECT_TRUE(OSTryLockMutex(&first));
  EXPECT_EQ(first.thread, current);
  EXPECT_EQ(first.count, 2);
  EXPECT_EQ(current->queueMutex.head, &first);
  EXPECT_EQ(current->queueMutex.tail, &first);
  EXPECT_EQ(first.link.next, nullptr);
  OSLockMutex(&second);
  EXPECT_EQ(first.link.next, &second);
  EXPECT_EQ(second.link.prev, &first);
  OSUnlockMutex(&first);
  EXPECT_EQ(first.count, 1);
  EXPECT_EQ(current->queueMutex.head, &first);
  OSUnlockMutex(&first);
  EXPECT_EQ(first.thread, nullptr);
  EXPECT_EQ(current->queueMutex.head, &second);
  EXPECT_EQ(second.link.prev, nullptr);
  OSUnlockMutex(&second);
  EXPECT_EQ(current->queueMutex.head, nullptr);
  EXPECT_EQ(current->queueMutex.tail, nullptr);
}

TEST(OSMutexTest, NonOwnerTryFailsAndUnlockDoesNotChangeCount) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  OSLockMutex(&mutex);
  OSLockMutex(&mutex);
  auto* owner = OSGetCurrentThread();
  std::thread other([&] {
    EXPECT_NE(OSGetCurrentThread(), owner);
    EXPECT_FALSE(OSTryLockMutex(&mutex));
    OSUnlockMutex(&mutex);
    EXPECT_TRUE(observe([&] { return mutex.thread == owner && mutex.count == 2; }));
  });
  other.join();
  OSUnlockMutex(&mutex);
  OSUnlockMutex(&mutex);
}

TEST(OSMutexTest, CountArithmeticWrapsLikePowerPC) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  OSLockMutex(&mutex);
  mutex.count = std::numeric_limits<s32>::max();
  EXPECT_TRUE(OSTryLockMutex(&mutex));
  EXPECT_EQ(mutex.count, std::numeric_limits<s32>::min());
  OSUnlockMutex(&mutex);
  EXPECT_EQ(mutex.count, std::numeric_limits<s32>::max());
  mutex.count = 1;
  OSUnlockMutex(&mutex);
}

TEST(OSMutexTest, ContendedSleepReleasesGateAndPreservesDisabledInterruptBit) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  OSLockMutex(&mutex);
  std::thread other([&] {
    const auto saved = OSDisableInterrupts();
    EXPECT_EQ(saved, TRUE);
    OSLockMutex(&mutex);
    EXPECT_EQ(OSDisableInterrupts(), FALSE);
    EXPECT_EQ(mutex.thread, OSGetCurrentThread());
    EXPECT_EQ(OSGetCurrentThread()->mutex, nullptr);
    OSUnlockMutex(&mutex);
    EXPECT_EQ(OSRestoreInterrupts(saved), FALSE);
  });
  EXPECT_TRUE(await([&] { return mutex.queue.head != nullptr; }));
  EXPECT_TRUE(observe([&] {
    return mutex.queue.head->state == OS_THREAD_STATE_WAITING && mutex.queue.head->mutex == &mutex &&
           mutex.queue.head->queue == &mutex.queue;
  }));
  OSUnlockMutex(&mutex);
  other.join();
  EXPECT_EQ(mutex.queue.head, nullptr);
  EXPECT_EQ(mutex.queue.tail, nullptr);
}

TEST(OSMutexTest, SchedulerCriticalSectionExcludesMutexQueries) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  EXPECT_EQ(OSDisableScheduler(), 0);
  std::promise<void> attempt;
  auto ready = attempt.get_future();
  auto result = std::async(std::launch::async, [&] {
    attempt.set_value();
    const auto locked = OSTryLockMutex(&mutex);
    if (locked) OSUnlockMutex(&mutex);
    return locked;
  });
  ready.wait();
  EXPECT_EQ(result.wait_for(20ms), std::future_status::timeout);
  EXPECT_EQ(OSEnableScheduler(), 1);
  EXPECT_TRUE(result.get());
}

TEST(OSMutexTest, PriorityDonationFollowsBlockedOwnerChainAndRestoresOnUnlock) {
  OSMutex first{}, second{};
  OSInitMutex(&first); OSInitMutex(&second);
  OSLockMutex(&first);
  auto* current = OSGetCurrentThread();
  std::atomic<OSThread*> middle{nullptr};
  std::atomic<OSThread*> high{nullptr};
  std::thread middle_worker([&] {
    auto* self = OSGetCurrentThread();
    EXPECT_TRUE(OSSetThreadPriority(self, 24));
    middle.store(self);
    OSLockMutex(&second);
    OSLockMutex(&first);
    OSUnlockMutex(&first);
    EXPECT_EQ(self->priority, 3); // Still inherits from the second mutex.
    OSUnlockMutex(&second);
    EXPECT_EQ(self->priority, 24);
  });
  EXPECT_TRUE(await([&] { return middle.load() && first.queue.head == middle.load(); }));
  std::thread high_worker([&] {
    auto* self = OSGetCurrentThread();
    EXPECT_TRUE(OSSetThreadPriority(self, 3));
    high.store(self);
    OSLockMutex(&second);
    OSUnlockMutex(&second);
  });
  EXPECT_TRUE(await([&] { return high.load() && second.queue.head == high.load(); }));
  EXPECT_TRUE(observe([&] {
    return middle.load()->priority == 3 && current->priority == 3 &&
           __OSGetEffectivePriority(current) == 3;
  }));
  OSUnlockMutex(&first);
  EXPECT_EQ(current->priority, current->base);
  middle_worker.join(); high_worker.join();
}

TEST(OSMutexTest, WaitQueueUsesPriorityThenArrivalAndReordersAfterDonation) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  OSLockMutex(&mutex);
  std::atomic<OSThread*> identities[3]{};
  std::vector<std::thread> threads;
  const int priorities[] = {20, 10, 20};
  for (int i = 0; i < 3; ++i) {
    threads.emplace_back([&, i] {
      auto* self = OSGetCurrentThread();
      OSSetThreadPriority(self, priorities[i]);
      identities[i].store(self);
      OSLockMutex(&mutex);
      OSUnlockMutex(&mutex);
    });
    EXPECT_TRUE(await([&] {
      auto* self = identities[i].load();
      return self && self->state == OS_THREAD_STATE_WAITING;
    }));
  }
  EXPECT_TRUE(observe([&] {
    return mutex.queue.head == identities[1].load() && mutex.queue.head->link.next == identities[0].load() &&
           mutex.queue.tail == identities[2].load();
  }));
  EXPECT_TRUE(OSSetThreadPriority(identities[2].load(), 5));
  EXPECT_TRUE(observe([&] {
    return mutex.queue.head == identities[2].load() && mutex.queue.head->link.next == identities[1].load() &&
           OSGetCurrentThread()->priority == 5;
  }));
  OSUnlockMutex(&mutex);
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(OSGetCurrentThread()->priority, 16);
}

TEST(OSMutexTest, OriginalUnlockAllReleasesRecursiveOwnersAndWakesAllQueues) {
  OSMutex first{}, second{};
  OSInitMutex(&first); OSInitMutex(&second);
  OSLockMutex(&first); OSLockMutex(&first); OSLockMutex(&second);
  std::atomic<int> acquired = 0;
  std::thread a([&] { OSLockMutex(&first); ++acquired; OSUnlockMutex(&first); });
  std::thread b([&] { OSLockMutex(&second); ++acquired; OSUnlockMutex(&second); });
  EXPECT_TRUE(await([&] { return first.queue.head && second.queue.head; }));
  const auto enabled = OSDisableInterrupts();
  __OSUnlockAllMutex(OSGetCurrentThread());
  EXPECT_EQ(first.thread, nullptr); EXPECT_EQ(first.count, 0);
  EXPECT_EQ(second.thread, nullptr); EXPECT_EQ(second.count, 0);
  EXPECT_EQ(OSGetCurrentThread()->queueMutex.head, nullptr);
  EXPECT_EQ(OSGetCurrentThread()->queueMutex.tail, nullptr);
  OSRestoreInterrupts(enabled);
  a.join(); b.join();
  EXPECT_EQ(acquired, 2);
}

TEST(OSMutexTest, NativeThreadExitRunsOriginalReleaseAllAndWakesWaiter) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  std::promise<void> held, release;
  auto held_future = held.get_future();
  auto release_future = release.get_future();
  std::thread owner([&] {
    OSLockMutex(&mutex); OSLockMutex(&mutex);
    held.set_value();
    release_future.wait();
    // Actual native thread exit deliberately retains recursive ownership.
  });
  held_future.wait();
  bool acquired = false;
  std::thread waiter([&] { OSLockMutex(&mutex); acquired = true; OSUnlockMutex(&mutex); });
  EXPECT_TRUE(await([&] { return mutex.queue.head != nullptr; }));
  release.set_value();
  owner.join(); waiter.join();
  EXPECT_TRUE(acquired);
  EXPECT_EQ(mutex.thread, nullptr); EXPECT_EQ(mutex.count, 0);
  EXPECT_EQ(mutex.queue.head, nullptr); EXPECT_EQ(mutex.queue.tail, nullptr);
}

TEST(OSMutexTest, RecursiveContentionProtectsSharedHeapLikeState) {
  OSMutex mutex{};
  OSInitMutex(&mutex);
  int used = 0;
  std::vector<std::thread> threads;
  for (int t = 0; t < 6; ++t) threads.emplace_back([&] {
    for (int i = 0; i < 1000; ++i) {
      OSLockMutex(&mutex);
      OSLockMutex(&mutex);
      ++used;
      OSUnlockMutex(&mutex);
      OSUnlockMutex(&mutex);
    }
  });
  for (auto& thread : threads) thread.join();
  EXPECT_EQ(used, 6000);
  EXPECT_EQ(mutex.thread, nullptr);
}

TEST(OSMutexDeathTest, ContentionCannotSilentlyReleaseDisabledScheduler) {
  EXPECT_DEATH({
    OSMutex mutex{};
    OSInitMutex(&mutex);
    std::promise<void> held;
    auto held_future = held.get_future();
    std::thread owner([&] {
      OSLockMutex(&mutex);
      held.set_value();
      for (;;) std::this_thread::sleep_for(10ms);
    });
    held_future.wait();
    OSDisableScheduler();
    OSLockMutex(&mutex);
  }, "blocking sleep while scheduler is disabled");
}
} // namespace
