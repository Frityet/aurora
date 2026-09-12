#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <aurora/vi.hpp>
#include <dolphin/os.h>
#include <dolphin/vi.h>
#include <gtest/gtest.h>
#include "dolphin/vi/vi_internal.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <thread>

// This fixture exercises the actual VI/OS implementation without a window.
// Only the platform's framebuffer-size notification is outside its scope.
namespace aurora::window {
void set_configured_frame_buffer_size(uint32_t, uint32_t) noexcept {}
void request_frame_buffer_resize() {}
}

namespace {
class VITest : public ::testing::Test {
protected:
  void SetUp() override {
    aurora::vi::shutdown();
    GXRenderModeObj mode{};
    mode.viTVmode = VI_TVMODE_NTSC_INT;
    mode.fbWidth = 640;
    mode.efbHeight = 480;
    VIConfigure(&mode);
    VIInit();
  }
  void TearDown() override { aurora::vi::shutdown(); }
};

TEST_F(VITest, RendererReadsPublishedGeometryWithoutWaitingForGuestCpu) {
  std::atomic<bool> completed{false};
  bool correct = false;
  std::thread decoder;
  bool finishedWhileGuestOwnedCpu;
  {
    const aurora::os::GuestThreadExecutionScope execution;
    GXRenderModeObj mode{};
    mode.fbWidth = 512;
    mode.efbHeight = 448;
    VIConfigure(&mode);
    decoder = std::thread([&] {
      const auto size = aurora::vi::configured_fb_size();
      correct = size.x == 512 && size.y == 448;
      completed.store(true, std::memory_order_release);
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    // Intentionally retain guest CPU ownership, as a caller changing the GX
    // FIFO breakpoint does while the renderer is decoding viewport commands.
    while (!completed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
      std::this_thread::yield();
    finishedWhileGuestOwnedCpu = completed.load(std::memory_order_acquire);
  }
  decoder.join();
  EXPECT_TRUE(finishedWhileGuestOwnedCpu);
  EXPECT_TRUE(correct);
}

struct CallbackTrace {
  OSMessageQueue messages{};
  std::array<OSMessage, 8> storage{};
  unsigned preCount = 0;
  unsigned postCount = 0;
  u32 preRetrace = 0;
  bool interruptsMasked = true;
  bool guestAllocations = true;
  void* preBuffer = nullptr;
  void* postBuffer = nullptr;
};
CallbackTrace* trace;

void pre_retrace(u32 count) {
  ++trace->preCount;
  trace->preRetrace = count;
  const auto enabled = OSDisableInterrupts();
  trace->interruptsMasked &= enabled == FALSE;
  OSRestoreInterrupts(enabled);
  trace->guestAllocations &= aurora::allocation::routing_state.guest;
  trace->preBuffer = VIGetCurrentFrameBuffer();
}
void post_retrace(u32 count) {
  ++trace->postCount;
  EXPECT_EQ(count, trace->preRetrace);
  EXPECT_EQ(count, VIGetRetraceCount());
  trace->postBuffer = VIGetCurrentFrameBuffer();
  OSSendMessage(&trace->messages, reinterpret_cast<OSMessage>(static_cast<uintptr_t>(count)), OS_MESSAGE_NOBLOCK);
}

TEST_F(VITest, ClockDeliversOriginalInterruptOrderWithoutCallerRetracePump) {
  const aurora::os::GuestThreadExecutionScope execution;
  CallbackTrace state;
  trace = &state;
  OSInitMessageQueue(&state.messages, state.storage.data(), state.storage.size());
  {
    const aurora::allocation::ClientAllocationScope allocations({true, true});
    VISetPreRetraceCallback(pre_retrace);
    VISetPostRetraceCallback(post_retrace);
  }
  std::array<u8, 32> buffer{};
  VISetNextFrameBuffer(buffer.data());
  EXPECT_EQ(VIGetNextFrameBuffer(), nullptr);
  VISetBlack(FALSE);
  VIFlush();
  EXPECT_EQ(VIGetNextFrameBuffer(), buffer.data());
  EXPECT_EQ(VIGetCurrentFrameBuffer(), nullptr);
  OSMessage message;
  OSReceiveMessage(&state.messages, &message, OS_MESSAGE_BLOCK);
  EXPECT_EQ(state.preBuffer, nullptr);
  EXPECT_EQ(state.postBuffer, buffer.data());
  EXPECT_EQ(state.preCount, 1);
  EXPECT_EQ(state.postCount, 1);
  EXPECT_TRUE(state.interruptsMasked);
  EXPECT_TRUE(state.guestAllocations);
  EXPECT_FALSE(aurora::vi::scanout_state().black);
  for (unsigned i = 0; i < 3; ++i) OSReceiveMessage(&state.messages, &message, OS_MESSAGE_BLOCK);
  EXPECT_EQ(state.postCount, 4);
  VISetPreRetraceCallback(nullptr);
  VISetPostRetraceCallback(nullptr);
  trace = nullptr;
}

TEST_F(VITest, FlushSeparatesRequestedNextAndScanningFramebuffers) {
  const aurora::os::GuestThreadExecutionScope execution;
  std::array<u8, 32> a{}, b{};
  VISetNextFrameBuffer(a.data());
  VIFlush();
  VIWaitForRetrace();
  EXPECT_EQ(VIGetCurrentFrameBuffer(), a.data());
  VISetNextFrameBuffer(b.data());
  EXPECT_EQ(VIGetNextFrameBuffer(), a.data());
  VIWaitForRetrace();
  EXPECT_EQ(VIGetCurrentFrameBuffer(), a.data());
  VIFlush();
  EXPECT_EQ(VIGetNextFrameBuffer(), b.data());
  EXPECT_EQ(VIGetCurrentFrameBuffer(), a.data());
  VIWaitForRetrace();
  EXPECT_EQ(VIGetCurrentFrameBuffer(), b.data());
}

TEST_F(VITest, GuestCpuDefersInterruptAndWaitRestoresInterruptState) {
  const aurora::os::GuestThreadExecutionScope execution;
  const auto before = VIGetRetraceCount();
  // Deliberately retain the guest CPU across a physical retrace deadline.
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
  EXPECT_EQ(VIGetRetraceCount(), before);
  const BOOL enabled = OSDisableInterrupts();
  VIWaitForRetrace();
  EXPECT_GT(VIGetRetraceCount(), before);
  EXPECT_EQ(OSDisableInterrupts(), FALSE);
  OSRestoreInterrupts(enabled);
  const auto initialized = VIGetRetraceCount();
  VIInit();
  EXPECT_EQ(VIGetRetraceCount(), initialized);
}

struct YieldingCallback {
  OSMessageQueue full{}, entered{};
  OSMessage fullStorage{}, enteredStorage{};
  unsigned count = 0;
  bool unregisterStarted = false;
  bool completed = false;
};
YieldingCallback* yielding;
void yielding_retrace(u32) {
  ++yielding->count;
  OSSendMessage(&yielding->entered, yielding, OS_MESSAGE_NOBLOCK);
  OSSendMessage(&yielding->full, yielding, OS_MESSAGE_BLOCK);
  yielding->completed = true;
}
void* release_callback(void* argument) {
  auto& state = *static_cast<YieldingCallback*>(argument);
  EXPECT_TRUE(state.unregisterStarted);
  OSMessage message;
  OSReceiveMessage(&state.full, &message, OS_MESSAGE_BLOCK);
  return nullptr;
}

TEST_F(VITest, UnregisterWaitsCooperativelyForYieldingOriginalCallback) {
  const aurora::os::GuestThreadExecutionScope execution;
  YieldingCallback state;
  yielding = &state;
  OSInitMessageQueue(&state.full, &state.fullStorage, 1);
  OSInitMessageQueue(&state.entered, &state.enteredStorage, 1);
  OSSendMessage(&state.full, nullptr, OS_MESSAGE_NOBLOCK);
  VISetPostRetraceCallback(yielding_retrace);
  OSMessage entered;
  OSReceiveMessage(&state.entered, &entered, OS_MESSAGE_BLOCK);
  OSThread release{};
  alignas(32) std::array<u8, 0x8000> stack{};
  ASSERT_TRUE(OSCreateThread(&release, release_callback, &state, stack.data() + stack.size(), stack.size(), 17, 0));
  OSResumeThread(&release);
  state.unregisterStarted = true;
  EXPECT_EQ(VISetPostRetraceCallback(nullptr), yielding_retrace);
  EXPECT_TRUE(state.completed);
  EXPECT_TRUE(OSJoinThread(&release, nullptr));
  VIWaitForRetrace();
  EXPECT_EQ(state.count, 1);
  yielding = nullptr;
}

TEST_F(VITest, ShutdownRetiresPointersAndInitStartsNewLifetime) {
  const aurora::os::GuestThreadExecutionScope execution;
  std::array<u8, 32> buffer{};
  VISetNextFrameBuffer(buffer.data());
  VIFlush();
  VIWaitForRetrace();
  aurora::vi::shutdown();
  const auto state = aurora::vi::scanout_state();
  EXPECT_FALSE(state.initialized);
  EXPECT_TRUE(state.black);
  EXPECT_EQ(state.frame_buffer, nullptr);
  VIInit();
  EXPECT_EQ(VIGetRetraceCount(), 0);
  VIWaitForRetrace();
  EXPECT_EQ(VIGetRetraceCount(), 1);
}
}
