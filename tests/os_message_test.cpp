#include <dolphin/os.h>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

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
OSMessage message(uintptr_t value) { return reinterpret_cast<OSMessage>(value); }

TEST(OSMessageTest, OriginalRingJamNullDiscardAndNonblockingPredicates) {
  OSMessageQueue queue;
  OSMessage slots[3];
  OSInitMessageQueue(&queue, slots, 3);
  EXPECT_EQ(queue.queueSend.head, nullptr);
  EXPECT_EQ(queue.queueReceive.head, nullptr);
  EXPECT_EQ(queue.msgArray, slots);
  EXPECT_EQ(queue.firstIndex, 0);
  EXPECT_EQ(queue.usedCount, 0);
  OSMessage result = message(99);
  EXPECT_FALSE(OSReceiveMessage(&queue, &result, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(result, message(99));
  EXPECT_TRUE(OSSendMessage(&queue, message(1), OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSSendMessage(&queue, message(2), OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSJamMessage(&queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_FALSE(OSSendMessage(&queue, message(3), OS_MESSAGE_NOBLOCK));
  EXPECT_FALSE(OSJamMessage(&queue, message(3), OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSReceiveMessage(&queue, &result, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(result, nullptr);
  EXPECT_TRUE(OSReceiveMessage(&queue, nullptr, OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSSendMessage(&queue, message(3), OS_MESSAGE_NOBLOCK));
  EXPECT_TRUE(OSReceiveMessage(&queue, &result, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(result, message(2));
  EXPECT_TRUE(OSReceiveMessage(&queue, &result, OS_MESSAGE_NOBLOCK));
  EXPECT_EQ(result, message(3));
  EXPECT_EQ(queue.usedCount, 0);
}

TEST(OSMessageTest, BlockingReceiverUsesActualThreadQueueAndRestoresInterrupts) {
  OSMessageQueue queue;
  OSMessage slots[1];
  OSInitMessageQueue(&queue, slots, 1);
  std::atomic<bool> received{false};
  std::thread worker([&] {
    const auto enabled = OSDisableInterrupts();
    EXPECT_TRUE(enabled);
    OSMessage value;
    EXPECT_TRUE(OSReceiveMessage(&queue, &value, OS_MESSAGE_BLOCK));
    EXPECT_EQ(value, message(7));
    EXPECT_FALSE(OSDisableInterrupts());
    OSRestoreInterrupts(enabled);
    received = true;
  });
  EXPECT_TRUE(await([&] {
    return queue.queueReceive.head && queue.queueReceive.head->queue == &queue.queueReceive &&
           queue.queueReceive.head->state == OS_THREAD_STATE_WAITING;
  }));
  EXPECT_FALSE(received.load());
  EXPECT_TRUE(OSSendMessage(&queue, message(7), OS_MESSAGE_NOBLOCK));
  worker.join();
  EXPECT_TRUE(received.load());
  EXPECT_EQ(queue.queueReceive.head, nullptr);
  EXPECT_EQ(queue.queueReceive.tail, nullptr);
}

TEST(OSMessageTest, FullBlockingJamWakesAfterReceiveAndPrepends) {
  OSMessageQueue queue;
  OSMessage slots[2];
  OSInitMessageQueue(&queue, slots, 2);
  OSSendMessage(&queue, message(1), 0);
  OSSendMessage(&queue, message(2), 0);
  std::thread worker([&] { EXPECT_TRUE(OSJamMessage(&queue, message(3), OS_MESSAGE_BLOCK)); });
  EXPECT_TRUE(await([&] { return queue.queueSend.head != nullptr; }));
  OSMessage value;
  EXPECT_TRUE(OSReceiveMessage(&queue, &value, 0));
  EXPECT_EQ(value, message(1));
  worker.join();
  EXPECT_TRUE(OSReceiveMessage(&queue, &value, 0));
  EXPECT_EQ(value, message(3));
  EXPECT_TRUE(OSReceiveMessage(&queue, &value, 0));
  EXPECT_EQ(value, message(2));
  EXPECT_EQ(queue.queueSend.head, nullptr);
  EXPECT_EQ(queue.queueSend.tail, nullptr);
}

TEST(OSMessageTest, WakeWithoutMessageRechecksPredicateThenShutdownMessagesDrainWaiters) {
  OSMessageQueue queue;
  OSMessage slots[1];
  OSInitMessageQueue(&queue, slots, 1);
  std::atomic<int> complete{0};
  auto run = [&] {
    OSMessage value;
    EXPECT_TRUE(OSReceiveMessage(&queue, &value, OS_MESSAGE_BLOCK));
    EXPECT_EQ(value, message(0xFFFF));
    ++complete;
  };
  std::thread first(run), second(run);
  EXPECT_TRUE(await([&] { return queue.queueReceive.head && queue.queueReceive.head->link.next; }));
  OSWakeupThread(&queue.queueReceive);
  EXPECT_TRUE(await([&] { return queue.queueReceive.head && queue.queueReceive.head->link.next; }));
  EXPECT_EQ(complete.load(), 0);
  // OS has no queue-close primitive: owners send their real protocol termination messages.
  EXPECT_TRUE(OSSendMessage(&queue, message(0xFFFF), OS_MESSAGE_BLOCK));
  EXPECT_TRUE(OSSendMessage(&queue, message(0xFFFF), OS_MESSAGE_BLOCK));
  first.join(); second.join();
  EXPECT_EQ(complete.load(), 2);
  EXPECT_EQ(queue.usedCount, 0);
  EXPECT_EQ(queue.queueReceive.head, nullptr);
  EXPECT_EQ(queue.queueSend.head, nullptr);
}

TEST(OSMessageTest, BlockingProducerConsumerPreserveOrderAcrossRepeatedWraps) {
  OSMessageQueue queue;
  OSMessage slots[3];
  OSInitMessageQueue(&queue, slots, 3);
  std::thread producer([&] {
    for (uintptr_t i = 0; i < 2000; ++i) EXPECT_TRUE(OSSendMessage(&queue, message(i), OS_MESSAGE_BLOCK));
  });
  for (uintptr_t i = 0; i < 2000; ++i) {
    OSMessage value;
    EXPECT_TRUE(OSReceiveMessage(&queue, &value, OS_MESSAGE_BLOCK));
    EXPECT_EQ(value, message(i));
  }
  producer.join();
  EXPECT_EQ(queue.usedCount, 0);
  EXPECT_EQ(queue.queueSend.head, nullptr);
  EXPECT_EQ(queue.queueReceive.head, nullptr);
}
} // namespace
