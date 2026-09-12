#include "gx_test_common.hpp"
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <dolphin/os.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

namespace {
using namespace std::chrono_literals;
namespace fifo = aurora::gx::fifo;

std::atomic<u32> sBreakCount{0};
std::atomic<u32> sFirstCommand{0};
std::atomic<u32> sLastCommand{0};
std::atomic<bool> sCallbackEntered{false};
std::atomic<bool> sCallbackMayReturn{false};
std::atomic<bool> sCallbackReturned{false};
std::atomic<bool> sCallbackGuestAllocation{false};
std::atomic<bool> sCallbackNestedGuestAllocation{false};
std::atomic<u32> sInterruptCallbacks{0};
std::atomic<bool> sCallbackSawEnabledInterrupts{false};
OSMessageQueue* sCallbackQueue = nullptr;
void* sNextBreak = nullptr;

void breakpoint_callback() {
  const u32 command = aurora::gx::g_gxState.bpRegCache[0x40];
  if (sBreakCount.load(std::memory_order_relaxed) == 0) sFirstCommand.store(command, std::memory_order_relaxed);
  sLastCommand.store(command, std::memory_order_relaxed);
  sBreakCount.fetch_add(1, std::memory_order_release);
}

void rearming_breakpoint_callback() {
  const bool first = sBreakCount.load(std::memory_order_relaxed) == 0;
  breakpoint_callback();
  if (first) GXEnableBreakPt(sNextBreak);
}

void blocking_callback() {
  sCallbackEntered.store(true, std::memory_order_release);
  while (!sCallbackMayReturn.load(std::memory_order_acquire)) std::this_thread::yield();
  sCallbackReturned.store(true, std::memory_order_release);
}

void routed_breakpoint_callback() {
  sCallbackGuestAllocation.store(aurora::allocation::routing_state.guest, std::memory_order_relaxed);
  sCallbackNestedGuestAllocation.store(aurora::allocation::routing_state.callbackGuest, std::memory_order_relaxed);
  GXDisableBreakPt();
  breakpoint_callback();
}

void queue_sending_callback(u16) {
  sCallbackEntered.store(true, std::memory_order_release);
  OSSendMessage(sCallbackQueue, reinterpret_cast<OSMessage>(2), OS_MESSAGE_BLOCK);
  sCallbackReturned.store(true, std::memory_order_release);
}

void interrupt_callback() {
  if (OSDisableInterrupts()) sCallbackSawEnabledInterrupts.store(true, std::memory_order_relaxed);
  // Leave the interrupt mask disabled: the ISR boundary restores the worker
  // context independently of the callback's final interrupt state.
  sInterruptCallbacks.fetch_add(1, std::memory_order_release);
}

void interrupt_token_callback(u16) { interrupt_callback(); }
void interrupt_breakpoint_callback() {
  GXDisableBreakPt();
  interrupt_callback();
}

template <typename Predicate>
bool wait_until(Predicate&& predicate) {
  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) return false;
    std::this_thread::yield();
  }
  return true;
}

void write_bp(u32 command) {
  fifo::write_u8(GX_LOAD_BP_REG);
  fifo::write_u32(command);
}

void write_nops(u32 count) {
  const std::vector<u8> nops(count, GX_NOP);
  fifo::write_data(nops.data(), count);
}

void* write_pointer() {
  GXFifoObj object;
  if (!GXGetCPUFifo(&object)) return nullptr;
  void* read;
  void* write;
  GXGetFifoPtrs(&object, &read, &write);
  return write;
}

class GXFifoBreakpointTest : public GXFifoTest {
protected:
  void SetUp() override {
    GXFifoTest::SetUp();
    GXSetBreakPtCallback(nullptr);
    GXSetDrawDoneCallback(nullptr);
    GXSetDrawSyncCallback(nullptr);
    sBreakCount.store(0, std::memory_order_relaxed);
    sFirstCommand.store(0, std::memory_order_relaxed);
    sLastCommand.store(0, std::memory_order_relaxed);
    sCallbackEntered.store(false, std::memory_order_relaxed);
    sCallbackMayReturn.store(false, std::memory_order_relaxed);
    sCallbackReturned.store(false, std::memory_order_relaxed);
    sCallbackGuestAllocation.store(false, std::memory_order_relaxed);
    sCallbackNestedGuestAllocation.store(false, std::memory_order_relaxed);
    sInterruptCallbacks.store(0, std::memory_order_relaxed);
    sCallbackSawEnabledInterrupts.store(false, std::memory_order_relaxed);
    sNextBreak = nullptr;
  }

  void TearDown() override {
    sCallbackMayReturn.store(true, std::memory_order_release);
    GXDisableBreakPt();
    GXSetBreakPtCallback(nullptr);
    GXSetDrawDoneCallback(nullptr);
    GXSetDrawSyncCallback(nullptr);
    GXFifoTest::TearDown();
  }
};

TEST_F(GXFifoBreakpointTest, StopsBeforeFollowingCommandsAndDisableResumes) {
  write_bp(0x40000011);
  void* saved = write_pointer();
  write_bp(0x40000017);
  EXPECT_EQ(GXSetBreakPtCallback(breakpoint_callback), nullptr);
  GXEnableBreakPt(saved);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  EXPECT_EQ(sLastCommand.load(std::memory_order_relaxed), 0x40000011u);
  GXFifoObj object;
  ASSERT_TRUE(GXGetGPFifo(&object));
  void* read;
  void* write;
  GXGetFifoPtrs(&object, &read, &write);
  EXPECT_EQ(read, saved);
  EXPECT_EQ(GXGetFifoCount(&object), 5u);
  GXBool over, under, readIdle, commandIdle, breakpoint;
  GXGetGPStatus(&over, &under, &readIdle, &commandIdle, &breakpoint);
  EXPECT_TRUE(breakpoint);
  EXPECT_TRUE(commandIdle);
  EXPECT_FALSE(readIdle);
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(sBreakCount.load(), 1u);
  GXGetGPStatus(&over, &under, &readIdle, &commandIdle, &breakpoint);
  EXPECT_FALSE(breakpoint);
  EXPECT_TRUE(readIdle);
  EXPECT_TRUE(commandIdle);
  EXPECT_EQ(GXSetBreakPtCallback(nullptr), breakpoint_callback);
}

TEST_F(GXFifoBreakpointTest, CallbackCanRearmAtTheNextBoundary) {
  write_bp(0x40000011);
  void* first = write_pointer();
  write_bp(0x40000012);
  sNextBreak = write_pointer();
  write_bp(0x40000017);
  GXSetBreakPtCallback(rearming_breakpoint_callback);
  GXEnableBreakPt(first);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 2; }));
  EXPECT_EQ(sFirstCommand.load(), 0x40000011u);
  EXPECT_EQ(sLastCommand.load(), 0x40000012u);
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 10u);
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  EXPECT_EQ(sBreakCount.load(), 2u);
}

TEST_F(GXFifoBreakpointTest, UnhandledHitStopsAndRearmingCurrentAddressIssuesOneNewInterrupt) {
  void* current = write_pointer();
  write_bp(0x40000017);
  GXEnableBreakPt(current);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().breakpoint; }));
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 0u);
  // Retire the asynchronous callback-less interrupt before installing one.
  // Merely observing its stop flag does not mean its interrupt handler ran.
  GXDisableBreakPt();
  fifo::drain();
  current = write_pointer();
  EXPECT_EQ(GXSetBreakPtCallback(breakpoint_callback), nullptr);
  EXPECT_EQ(sBreakCount.load(), 0u);
  GXEnableBreakPt(current);
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  GXEnableBreakPt(current);
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 2; }));
  write_nops(32);
  fifo::publish();
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 5u);
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(sBreakCount.load(), 2u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, SavedDefaultAddressesSurviveGrowthDrainAndWrap) {
  GXFifoObj before;
  ASSERT_TRUE(GXGetCPUFifo(&before));
  auto* base = static_cast<u8*>(GXGetFifoBase(&before));
  const u32 ringSize = GXGetFifoSize(&before);
  write_bp(0x40000011);
  void* saved = write_pointer();
  write_nops(ringSize + 32);
  write_bp(0x40000017);
  GXFifoObj grown;
  ASSERT_TRUE(GXGetCPUFifo(&grown));
  EXPECT_EQ(GXGetFifoBase(&grown), base);
  EXPECT_EQ(GXGetFifoSize(&grown), ringSize);
  EXPECT_EQ(write_pointer(), base + 42);
  EXPECT_TRUE(GXGetFifoWrap(&grown));
  GXSetBreakPtCallback(breakpoint_callback);
  GXEnableBreakPt(saved);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  EXPECT_EQ(sLastCommand.load(), 0x40000011u);
  GXDisableBreakPt();
  fifo::drain();
  GXFifoObj drained;
  ASSERT_TRUE(GXGetCPUFifo(&drained));
  EXPECT_EQ(GXGetFifoBase(&drained), base);
  EXPECT_EQ(GXGetFifoSize(&drained), ringSize);
  EXPECT_EQ(GXGetFifoCount(&drained), 0u);
  EXPECT_EQ(write_pointer(), base + 42);
  write_nops(3);
  EXPECT_EQ(write_pointer(), base + 45);
  fifo::drain();
}

TEST_F(GXFifoBreakpointTest, PassedAddressRefersToNextLapOfSuppliedRing) {
  alignas(32) std::array<u8, 64> ring{};
  GXFifoObj object;
  GXInitFifoBase(&object, ring.data(), ring.size());
  GXInitFifoPtrs(&object, ring.data() + 56, ring.data() + 56);
  GXSetCPUFifo(&object);
  GXSetGPFifo(&object);
  write_nops(4);
  void* saved = write_pointer();
  EXPECT_EQ(saved, ring.data() + 60);
  write_nops(4);
  fifo::drain();
  EXPECT_EQ(write_pointer(), ring.data());
  GXSetBreakPtCallback(breakpoint_callback);
  GXEnableBreakPt(saved);
  write_nops(60);
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  EXPECT_EQ(fifo::cursor_snapshot().consumed, 68u);
  EXPECT_EQ(sLastCommand.load(), 0u);
  ASSERT_TRUE(GXGetGPFifo(&object));
  void* read;
  void* write;
  GXGetFifoPtrs(&object, &read, &write);
  EXPECT_EQ(read, saved);
  EXPECT_EQ(write, ring.data() + 1);
  EXPECT_EQ(GXGetFifoCount(&object), 5u);
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, CanArmAgainstDecodedCursorWhileEarlierCallbackIsRunning) {
  write_bp(0x40000011);
  write_bp(0x45000002); // Draw-done callback pauses the decoder after this command.
  void* saved = write_pointer();
  write_bp(0x40000017);
  GXSetDrawDoneCallback(blocking_callback);
  GXSetBreakPtCallback(breakpoint_callback);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sCallbackEntered.load(std::memory_order_acquire); }));
  const auto duringCallback = fifo::cursor_snapshot();
  EXPECT_EQ(duringCallback.consumed, 10u);
  EXPECT_EQ(duringCallback.completed, 0u);
  GXEnableBreakPt(saved);
  sCallbackMayReturn.store(true, std::memory_order_release);
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  EXPECT_TRUE(sCallbackReturned.load(std::memory_order_acquire));
  EXPECT_EQ(sLastCommand.load(), 0x40000011u);
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, UnregisterWaitsForAnInFlightBreakpointCallback) {
  GXSetBreakPtCallback(blocking_callback);
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sCallbackEntered.load(std::memory_order_acquire); }));
  auto unregister = std::async(std::launch::async, [] { return GXSetBreakPtCallback(nullptr); });
  EXPECT_EQ(unregister.wait_for(25ms), std::future_status::timeout);
  sCallbackMayReturn.store(true, std::memory_order_release);
  EXPECT_EQ(unregister.get(), blocking_callback);
  EXPECT_TRUE(sCallbackReturned.load(std::memory_order_acquire));
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, ShutdownRetiresTheDefaultAddressSpaceAndAttachment) {
  fifo::shutdown();
  GXFifoObj object;
  EXPECT_FALSE(GXGetCPUFifo(&object));
  EXPECT_FALSE(GXGetGPFifo(&object));
  EXPECT_FALSE(fifo::cursor_snapshot().active);
  EXPECT_EQ(fifo::cursor_snapshot().addressBase, nullptr);
  fifo::init();
  GXInit(nullptr, 0);
  fifo::clear_buffer();
  EXPECT_TRUE(GXGetCPUFifo(&object));
  EXPECT_EQ(GXGetFifoCount(&object), 0u);
  GXSetBreakPtCallback(breakpoint_callback);
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sBreakCount.load(std::memory_order_acquire) == 1; }));
  GXDisableBreakPt();
  fifo::drain();
}

TEST_F(GXFifoBreakpointTest, DrainYieldsGuestExecutionAndRestoresCapturedCallbackRouting) {
  struct RestoreRouting {
    aurora::allocation::RoutingState previous = aurora::allocation::routing_state;
    ~RestoreRouting() { aurora::allocation::routing_state = previous; }
  } restoreRouting;
  const aurora::os::GuestThreadExecutionScope execution;
  aurora::allocation::routing_state = {true, true};
  GXSetBreakPtCallback(routed_breakpoint_callback);
  aurora::allocation::routing_state = {false, false};
  write_bp(0x40000011);
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().breakpoint; }));
  EXPECT_EQ(sBreakCount.load(), 0u);
  fifo::drain();
  EXPECT_EQ(sBreakCount.load(std::memory_order_acquire), 1u);
  EXPECT_EQ(sLastCommand.load(), 0x40000011u);
  EXPECT_TRUE(sCallbackGuestAllocation.load());
  EXPECT_TRUE(sCallbackNestedGuestAllocation.load());
  EXPECT_FALSE(aurora::allocation::routing_state.guest);
  EXPECT_FALSE(aurora::allocation::routing_state.callbackGuest);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
  // Callback replacement while owning the CPU must not invert its mutex order.
  EXPECT_EQ(GXSetBreakPtCallback(nullptr), routed_breakpoint_callback);
}

TEST_F(GXFifoBreakpointTest, GuestDisableCancelsAnInterruptWaitingForCpuOwnership) {
  const aurora::os::GuestThreadExecutionScope execution;
  GXSetBreakPtCallback(breakpoint_callback);
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().breakpoint; }));
  GXDisableBreakPt();
  fifo::drain();
  EXPECT_EQ(sBreakCount.load(std::memory_order_acquire), 0u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, GuestRearmReplacesAnInterruptWaitingForCpuOwnership) {
  const aurora::os::GuestThreadExecutionScope execution;
  GXSetBreakPtCallback(routed_breakpoint_callback);
  void* first = write_pointer();
  write_bp(0x40000011);
  void* next = write_pointer();
  write_bp(0x40000017);
  GXEnableBreakPt(first);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().breakpoint; }));
  GXEnableBreakPt(next);
  fifo::drain();
  EXPECT_EQ(sBreakCount.load(std::memory_order_acquire), 1u);
  EXPECT_EQ(sLastCommand.load(), 0x40000011u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, GuestUnregisterYieldsWhileCallbackSleepsOnAFullSdkQueue) {
  OSMessageQueue queue;
  OSMessage storage[1];
  OSInitMessageQueue(&queue, storage, 1);
  ASSERT_TRUE(OSSendMessage(&queue, reinterpret_cast<OSMessage>(1), OS_MESSAGE_NOBLOCK));
  sCallbackQueue = &queue;
  GXSetDrawSyncCallback(queue_sending_callback);
  write_bp(0x48000001); // PE token interrupt.
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return sCallbackEntered.load(std::memory_order_acquire); }));
  {
    const aurora::os::GuestThreadExecutionScope execution;
    ASSERT_NE(queue.queueSend.head, nullptr);
    auto consumer = std::async(std::launch::async, [&] {
      const aurora::os::GuestThreadExecutionScope execution;
      OSMessage message = nullptr;
      OSReceiveMessage(&queue, &message, OS_MESSAGE_BLOCK);
      return message;
    });
    // This setter owns the guest CPU. Its cooperative mutex wait must let the
    // consumer run, wake the blocked callback, and complete its unregister.
    EXPECT_EQ(GXSetDrawSyncCallback(nullptr), queue_sending_callback);
    EXPECT_TRUE(sCallbackReturned.load(std::memory_order_acquire));
    {
      const aurora::os::GuestThreadWaitScope wait;
      EXPECT_EQ(consumer.get(), reinterpret_cast<OSMessage>(1));
    }
    OSMessage message = nullptr;
    EXPECT_TRUE(OSReceiveMessage(&queue, &message, OS_MESSAGE_NOBLOCK));
    EXPECT_EQ(message, reinterpret_cast<OSMessage>(2));
    fifo::drain();
  }
  sCallbackQueue = nullptr;
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, AllCallbacksEnterWithInterruptsDisabledAndRestoreWorkerState) {
  GXSetDrawSyncCallback(interrupt_token_callback);
  GXSetDrawDoneCallback(interrupt_callback);
  GXSetBreakPtCallback(interrupt_breakpoint_callback);
  write_bp(0x48000001); // PE token interrupt.
  write_bp(0x45000002); // PE finish interrupt.
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  fifo::begin_frame();
  fifo::publish();
  fifo::drain();
  EXPECT_EQ(sInterruptCallbacks.load(std::memory_order_acquire), 3u);
  EXPECT_FALSE(sCallbackSawEnabledInterrupts.load());
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000017u);
}

TEST_F(GXFifoBreakpointTest, AlarmAbortDiscardsStoppedCommandsAndPreservesRingAddresses) {
  const aurora::os::GuestThreadExecutionScope execution;
  GXSetBreakPtCallback(breakpoint_callback);
  GXSetDrawSyncCallback(interrupt_token_callback);
  GXSetDrawDoneCallback(interrupt_callback);
  write_bp(0x40000011);
  GXEnableBreakPt(write_pointer());
  write_bp(0x40000017);
  write_bp(0x48000007);
  write_bp(0x45000002);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().breakpoint; }));
  const auto before = fifo::cursor_snapshot();
  void* savedWrite = write_pointer();
  {
    // The real watchdog is a scheduler-disabled alarm callback. None of these
    // control operations may yield the guest or wait for pending callbacks.
    const aurora::os::GuestInterruptExecutionScope interrupt;
    GXBool over, under, readIdle, commandIdle, breakpoint;
    GXGetGPStatus(&over, &under, &readIdle, &commandIdle, &breakpoint);
    EXPECT_TRUE(breakpoint);
    GXDisableBreakPt();
    GXAbortFrame();
  }
  write_bp(0x40000015);
  write_bp(0x48000009);
  write_bp(0x45000002);
  fifo::drain();
  const auto after = fifo::cursor_snapshot();
  EXPECT_EQ(after.generation, before.generation);
  EXPECT_EQ(after.addressBase, before.addressBase);
  EXPECT_GE(after.consumed, before.written);
  EXPECT_EQ(after.written, after.completed);
  EXPECT_EQ(write_pointer(), static_cast<u8*>(savedWrite) + 15);
  EXPECT_EQ(sBreakCount.load(), 0u);
  EXPECT_EQ(sInterruptCallbacks.load(), 2u);
  EXPECT_EQ(GXReadDrawSync(), 9u);
  EXPECT_EQ(aurora::gx::g_gxState.bpRegCache[0x40], 0x40000015u);
}

TEST_F(GXFifoBreakpointTest, AbortRevokesDrawCallbacksWaitingForGuestCpu) {
  const aurora::os::GuestThreadExecutionScope execution;
  GXSetDrawDoneCallback(interrupt_callback);
  write_bp(0x45000002);
  fifo::begin_frame();
  fifo::publish();
  ASSERT_TRUE(wait_until([] { return fifo::cursor_snapshot().consumed == 5; }));
  EXPECT_EQ(sInterruptCallbacks.load(), 0u);
  GXAbortFrame();
  write_bp(0x48000006);
  GXAbortFrame();
  write_bp(0x45000002);
  fifo::drain();
  EXPECT_EQ(sInterruptCallbacks.load(), 1u);
  EXPECT_EQ(fifo::cursor_snapshot().written, fifo::cursor_snapshot().completed);
}

TEST(GXSubmissionOwnership, AbortWinsBeforeCommitAndCommittedSubmissionSurvivesAbort) {
  using namespace aurora::gfx;
  auto pending = std::make_shared<SubmissionState>();
  std::promise<void> checked;
  std::promise<void> release;
  auto proceed = release.get_future();
  auto submit = std::async(std::launch::async, [&] {
    // Pause the submitting caller after its initial epoch check. The decision
    // itself must still compete atomically with abort at the native boundary.
    EXPECT_TRUE(pending->epoch.current());
    checked.set_value();
    proceed.wait();
    return pending->commit();
  });
  checked.get_future().wait();
  abandon_command_epoch();
  EXPECT_TRUE(pending->abandoned());
  release.set_value();
  EXPECT_FALSE(submit.get());
  pending->retire();

  auto committed = std::make_shared<SubmissionState>();
  ASSERT_TRUE(committed->commit());
  // This is the interval between the submission decision and the native
  // queue call. Abort must never transiently classify that owner as discarded.
  abandon_command_epoch();
  EXPECT_TRUE(committed->is_submitted());
  EXPECT_FALSE(committed->abandoned());
  for (unsigned i = 0; i != 64; ++i) {
    auto next = std::make_shared<SubmissionState>();
    ASSERT_TRUE(next->commit());
  }
  EXPECT_TRUE(committed->is_submitted());
}
} // namespace
