#include <dolphin/vi.h>
#include <dolphin/os.h>
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <aurora/vi.hpp>

#include "../../window.hpp"
#include "aurora/math.hpp"
#include "vi_internal.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace aurora::vi {
namespace {
using Clock = std::chrono::steady_clock;
using Nanoseconds = std::chrono::nanoseconds;
std::atomic<bool> s_dtvConnected{true};
std::optional<GXRenderModeObj> sRenderMode;
// The renderer/FIFO decoder reads published geometry without acquiring the
// guest CPU: decoding can hold locks that a guest GX call also needs.
std::atomic<uint64_t> sConfiguredFramebufferSize{(uint64_t{640} << 32) | 480};
u32 sRetraceCount = 0;
void* sRequestedFrameBuffer = nullptr;
void* sNextFrameBuffer = nullptr;
void* sCurrentFrameBuffer = nullptr;
BOOL sRequestedBlack = TRUE;
BOOL sNextBlack = TRUE;
BOOL sBlack = TRUE;
BOOL sDimmingEnabled = TRUE;
bool sFlushPending = false;
bool sInitialized = false;
OSThreadQueue sRetraceQueue{};
OSMutex sCallbackMutex{};
OSMutex sLifecycleMutex{};
std::once_flag sCallbackMutexInitialized;
struct Callback {
  VIRetraceCallback function = nullptr;
  aurora::allocation::RoutingState routing{};
};
Callback sPreCallback;
Callback sPostCallback;

struct CallbackLock {
  CallbackLock() {
    std::call_once(sCallbackMutexInitialized, [] { OSInitMutex(&sCallbackMutex); OSInitMutex(&sLifecycleMutex); });
    OSLockMutex(&sCallbackMutex);
  }
  ~CallbackLock() { OSUnlockMutex(&sCallbackMutex); }
};

struct LifecycleLock {
  LifecycleLock() {
    std::call_once(sCallbackMutexInitialized, [] { OSInitMutex(&sCallbackMutex); OSInitMutex(&sLifecycleMutex); });
    OSLockMutex(&sLifecycleMutex);
  }
  ~LifecycleLock() { OSUnlockMutex(&sLifecycleMutex); }
};

void invoke(const Callback& callback) {
  if (callback.function == nullptr) return;
  // A callback can replace itself, so copy its registration before dispatch.
  const auto registration = callback;
  const allocation::ClientAllocationScope routing(registration.routing);
  registration.function(sRetraceCount);
}

class RetraceClock {
public:
  ~RetraceClock() { stop(); }

  void start() {
    std::lock_guard lock(mutex_);
    if (worker_.joinable()) return;
    stopping_ = false;
    worker_ = std::thread([this] { run(); });
  }

  void set_period(Nanoseconds value) {
    std::lock_guard lock(mutex_);
    period_ = value;
    origin_ = Clock::now();
    ++revision_;
    changed_.notify_all();
  }

  u32 current_line(u32 halfLines) {
    std::lock_guard lock(mutex_);
    const auto elapsed = std::chrono::duration_cast<Nanoseconds>(Clock::now() - origin_).count();
    return static_cast<u32>((elapsed % period_.count()) * halfLines / period_.count()) >> 1;
  }

  void stop() {
    {
      std::lock_guard lock(mutex_);
      stopping_ = true;
      changed_.notify_all();
    }
    // Never keep the guest CPU while a retrace worker may be acquiring it.
    if (worker_.joinable()) {
      const os::GuestThreadWaitScope wait;
      worker_.join();
    }
  }

private:
  void run() {
    const allocation::HostAllocationScope host;
    std::unique_lock lock(mutex_);
    auto deadline = Clock::now() + period_;
    auto revision = revision_;
    while (!stopping_) {
      if (changed_.wait_until(lock, deadline, [&] { return stopping_ || revision != revision_; })) {
        revision = revision_;
        deadline = Clock::now() + period_;
        continue;
      }
      lock.unlock();
      {
        const os::GuestThreadExecutionScope execution;
        const CallbackLock callbacks;
        if (sInitialized) {
          const BOOL enabled = OSDisableInterrupts();
          ++sRetraceCount;
          invoke(sPreCallback);
          if (sFlushPending) {
            sCurrentFrameBuffer = sNextFrameBuffer;
            sBlack = sNextBlack;
            sFlushPending = false;
          }
          invoke(sPostCallback);
          OSWakeupThread(&sRetraceQueue);
          OSRestoreInterrupts(enabled);
        }
      }
      lock.lock();
      // Coalesce interrupts delayed by the guest CPU, like a pending hardware
      // interrupt; do not fabricate a burst of retraces after a long pause.
      const auto now = Clock::now();
      do { deadline += period_; } while (deadline <= now);
    }
  }

  std::mutex mutex_;
  std::condition_variable changed_;
  std::thread worker_;
  Nanoseconds period_{16'683'350}; // 59.94 Hz NTSC/EURGB60 video fields
  Clock::time_point origin_ = Clock::now();
  unsigned revision_ = 0;
  bool stopping_ = false;
};
RetraceClock& retrace_clock() { static RetraceClock value; return value; }

Vec2<uint32_t> render_mode_size() noexcept {
  if (!sRenderMode) return {640, 480};
  return {sRenderMode->fbWidth, sRenderMode->efbHeight};
}

VIRetraceCallback replace_callback(Callback& target, VIRetraceCallback callback) {
  const os::GuestThreadExecutionScope execution;
  const allocation::HostAllocationScope host;
  const CallbackLock lock;
  const auto previous = target.function;
  target = {callback, allocation::routing_state};
  return previous;
}
} // namespace

void set_dtv_connected(bool connected) noexcept { s_dtvConnected.store(connected, std::memory_order_relaxed); }
bool dtv_connected() noexcept { return s_dtvConnected.load(std::memory_order_relaxed); }

void configure(const GXRenderModeObj* rm) noexcept {
  const os::GuestThreadExecutionScope execution;
  const auto oldSize = render_mode_size();
  if (rm == nullptr) sRenderMode.reset();
  else sRenderMode = *rm;
  const auto newSize = render_mode_size();
  sConfiguredFramebufferSize.store((uint64_t{newSize.x} << 32) | newSize.y, std::memory_order_release);
  window::set_configured_frame_buffer_size(newSize.x, newSize.y);
  if (newSize != oldSize) window::request_frame_buffer_resize();
  const u32 format = rm == nullptr ? VI_NTSC : static_cast<u32>(rm->viTVmode) >> 2U;
  retrace_clock().set_period(format == VI_PAL || format == VI_DEBUG_PAL ? Nanoseconds(20'000'000) : Nanoseconds(16'683'350));
}

Vec2<uint32_t> configured_fb_size() noexcept {
  const auto size = sConfiguredFramebufferSize.load(std::memory_order_acquire);
  return {static_cast<uint32_t>(size >> 32), static_cast<uint32_t>(size)};
}

ScanoutState scanout_state() noexcept {
  const os::GuestThreadExecutionScope execution;
  return {sInitialized, sBlack != FALSE, sCurrentFrameBuffer};
}

void shutdown() noexcept {
  const os::GuestThreadExecutionScope execution;
  const LifecycleLock lifecycle;
  {
    const CallbackLock callbacks;
    sInitialized = false;
    sPreCallback = {};
    sPostCallback = {};
    sRequestedFrameBuffer = sNextFrameBuffer = sCurrentFrameBuffer = nullptr;
    sFlushPending = false;
    sBlack = sRequestedBlack = sNextBlack = TRUE;
    OSWakeupThread(&sRetraceQueue);
  }
  retrace_clock().stop();
}
} // namespace aurora::vi

extern "C" {
void VIInit() {
  using namespace aurora::vi;
  const aurora::os::GuestThreadExecutionScope execution;
  const aurora::allocation::HostAllocationScope host;
  if (sInitialized) return;
  const LifecycleLock lifecycle;
  const CallbackLock callbacks;
  if (sInitialized) return;
  sRetraceCount = 0;
  sRequestedFrameBuffer = sNextFrameBuffer = sCurrentFrameBuffer = nullptr;
  sRequestedBlack = sNextBlack = sBlack = TRUE;
  sDimmingEnabled = TRUE;
  sPreCallback = {};
  sPostCallback = {};
  sFlushPending = false;
  OSInitThreadQueue(&sRetraceQueue);
  sInitialized = true;
  retrace_clock().start();
}
void VIConfigure(const GXRenderModeObj* rm) { aurora::vi::configure(rm); }
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height) {
  using namespace aurora::vi;
  const aurora::os::GuestThreadExecutionScope execution;
  if (!sRenderMode) sRenderMode = GXRenderModeObj{};
  sRenderMode->viXOrigin = xOrg;
  sRenderMode->viYOrigin = yOrg;
  sRenderMode->viWidth = width;
  sRenderMode->viHeight = height;
}
u32 VIGetTvFormat() {
  using namespace aurora::vi;
  const aurora::os::GuestThreadExecutionScope execution;
  if (!sRenderMode) return VI_NTSC;
  const auto format = static_cast<u32>(sRenderMode->viTVmode) >> 2U;
  switch (format) {
  case 0: case 3: case 6: case 7: case 8: return VI_NTSC;
  case 1: case 4: return VI_PAL;
  default: return format;
  }
}
u32 VIGetScanMode() {
  const aurora::os::GuestThreadExecutionScope execution;
  const auto& mode = aurora::vi::sRenderMode;
  return mode ? static_cast<u32>(mode->viTVmode) & 3U : VI_INTERLACE;
}
u32 VIGetCurrentLine() {
  const aurora::os::GuestThreadExecutionScope execution;
  const auto scan = VIGetScanMode();
  const bool pal = VIGetTvFormat() == VI_PAL;
  // Original VIGetCurrentLine returns the current field's half-line / 2,
  // including blanking. These are the original VI timing-table nhlines.
  const u32 halfLines = scan == VI_PROGRESSIVE ? (pal ? 1250 : 1050) :
                       scan == VI_NON_INTERLACE ? (pal ? 624 : 526) : (pal ? 625 : 525);
  return aurora::vi::retrace_clock().current_line(halfLines);
}
u32 VIGetRetraceCount() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aurora::vi::sRetraceCount;
}
u32 VIGetNextField() { return VIGetRetraceCount() & 1U; }
u32 VIGetDTVStatus() { return aurora::vi::dtv_connected() ? 1U : 0U; }
void VIWaitForRetrace() {
  using namespace aurora::vi;
  const aurora::os::GuestThreadExecutionScope execution;
  VIInit();
  const BOOL enabled = OSDisableInterrupts();
  const auto count = sRetraceCount;
  do { OSSleepThread(&sRetraceQueue); } while (sInitialized && count == sRetraceCount);
  OSRestoreInterrupts(enabled);
}
void VIFlush() {
  using namespace aurora::vi;
  const aurora::os::GuestThreadExecutionScope execution;
  sNextFrameBuffer = sRequestedFrameBuffer;
  sNextBlack = sRequestedBlack;
  sFlushPending = true;
}
void* VIGetCurrentFrameBuffer() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aurora::vi::sCurrentFrameBuffer;
}
void* VIGetNextFrameBuffer() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aurora::vi::sNextFrameBuffer;
}
void VISetNextFrameBuffer(void* fb) {
  const aurora::os::GuestThreadExecutionScope execution;
  aurora::vi::sRequestedFrameBuffer = fb;
}
void VISetBlack(BOOL black) {
  const aurora::os::GuestThreadExecutionScope execution;
  aurora::vi::sRequestedBlack = black != FALSE ? TRUE : FALSE;
}
BOOL VIEnableDimming(BOOL enabled) {
  const aurora::os::GuestThreadExecutionScope execution;
  const auto previous = aurora::vi::sDimmingEnabled;
  aurora::vi::sDimmingEnabled = enabled != FALSE ? TRUE : FALSE;
  return previous;
}
BOOL VIResetDimmingCount() { return TRUE; }
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) { return aurora::vi::replace_callback(aurora::vi::sPreCallback, cb); }
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) { return aurora::vi::replace_callback(aurora::vi::sPostCallback, cb); }
}
