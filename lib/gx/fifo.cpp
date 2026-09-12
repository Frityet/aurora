#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>
#include <dolphin/os.h>
#include <dolphin/os/OSInterrupt.h>
#include <dolphin/os/OSMutex.h>

#include "fifo.hpp"

#include "../thread.hpp"
#include "command_processor.hpp"
#include "../gfx/recording.hpp"
#include "../gfx/depth_peek.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>

#include <tracy/Tracy.hpp>

namespace aurora::gx::fifo {
namespace detail {
uint8_t* sBufferData = nullptr;
uint32_t sBufferSize = 0;
uint32_t sBufferCapacity = 0;
std::atomic<uint64_t> sWritten{0};
bool sInDisplayList = false;
uint8_t* sDlBuffer = nullptr;
uint32_t sDlSize = 0;
uint32_t sDlWritePos = 0;
} // namespace detail

namespace {
constexpr Module Log{"aurora::gx::fifo"};
constexpr auto kProcessingMode = ProcessingMode::Thread;
constexpr uint32_t kDrawBatchSize = 1;

bool sFrameActive = false;
std::atomic<bool> sActive{false};
std::atomic<uint64_t> sGeneration{0};
std::atomic<uint8_t*> sAddressBuffer{nullptr};
constexpr uint32_t kAddressBufferSize = 64 * 1024;
uint32_t sPendingDraws = 0;
std::atomic<uint64_t> sPublished{0};
std::atomic<uint64_t> sDecoded{0};
std::atomic<uint64_t> sProcessed{0};
uint64_t sStreamBase = 0;
std::mutex sBufferMutex;
// Control-register writes and decoding have one ordering. Callbacks run after
// releasing this lock, so another thread can rearm a stopped FIFO from them.
std::mutex sExecutionMutex;
struct BreakPoint {
  uint64_t cursor = 0;
  uint64_t revision = 0;
  bool enabled = false;
  bool hit = false;
  bool notified = false;
} sBreakPoint;
std::atomic<uint64_t> sBreakPointRevision{1};
std::atomic<uint64_t> sBreakPointHitRevision{0};
std::atomic<uint64_t> sAbortFloor{0};
std::atomic<uint64_t> sAcknowledgedEpoch{1};
std::atomic<uint32_t> sWorkerWake{0};
thread::Thread sWorkerThread;
// A callback may sleep in an original SDK queue while keeping this recursive
// lock. Its setter must then yield the CPU while waiting for it to return.
// Zero initialization supplies OSInitMutex's queue, owner and count state;
// the intrusive owner links are assigned by the first OSLockMutex.
OSMutex sCallbackMutex{};
class CallbackLock {
public:
  CallbackLock() { OSLockMutex(&sCallbackMutex); }
  ~CallbackLock() { OSUnlockMutex(&sCallbackMutex); }
  CallbackLock(const CallbackLock&) = delete;
  CallbackLock& operator=(const CallbackLock&) = delete;
};
class CallbackInterruptScope {
public:
  CallbackInterruptScope() : previous_(OSDisableInterrupts()) {}
  ~CallbackInterruptScope() { OSRestoreInterrupts(previous_); }
  CallbackInterruptScope(const CallbackInterruptScope&) = delete;
  CallbackInterruptScope& operator=(const CallbackInterruptScope&) = delete;
private:
  BOOL previous_;
};
DrawDoneCallback sDrawDoneCallback = nullptr;
DrawSyncCallback sDrawSyncCallback = nullptr;
BreakPointCallback sBreakPointCallback = nullptr;
aurora::allocation::RoutingState sDrawDoneRouting;
aurora::allocation::RoutingState sDrawSyncRouting;
aurora::allocation::RoutingState sBreakPointRouting;
std::atomic<uint16_t> sDrawSyncToken{0};
struct CallbackEfbRead {
  AuroraDepthSnapshotId snapshot = AURORA_INVALID_DEPTH_SNAPSHOT_ID;
  ~CallbackEfbRead() {
    if (snapshot != AURORA_INVALID_DEPTH_SNAPSHOT_ID) gfx::depth_peek::release_snapshot(snapshot);
  }
};
thread_local CallbackEfbRead* sCallbackEfbRead = nullptr;

void dispatch_draw_done(gfx::CommandEpoch epoch) noexcept {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  if (!epoch.current()) return;
  if (const auto callback = sDrawDoneCallback; callback != nullptr) {
    {
      const CallbackInterruptScope interrupt;
      const aurora::allocation::ClientAllocationScope clientAllocations{sDrawDoneRouting};
      callback();
    }
  }
}

void wake_worker() noexcept {
  sWorkerWake.fetch_add(1, std::memory_order_release);
  sWorkerWake.notify_all();
}

void dispatch_draw_sync(uint16_t token, bool interrupt, gfx::CommandEpoch epoch) noexcept {
  if (!epoch.current()) return;
  gfx::complete_draw();
  if (!epoch.current()) return;
  sDrawSyncToken.store(token, std::memory_order_release);
  if (!interrupt) return;
  // Unregistering waits for any in-flight callback, including its EFB reads.
  // Take CPU ownership first: the caller may replace a callback while owning
  // the CPU, and must never wait for a callback holding this mutex to enter it.
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  if (!epoch.current() || sDrawSyncCallback == nullptr) return;
  CallbackEfbRead read;
  auto* previousRead = std::exchange(sCallbackEfbRead, &read);
  {
    const CallbackInterruptScope interrupt;
    const aurora::allocation::ClientAllocationScope clientAllocations{sDrawSyncRouting};
    sDrawSyncCallback(token);
  }
  sCallbackEfbRead = previousRead;
}

void dispatch_breakpoint(uint64_t revision) noexcept {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  // This check follows CPU acquisition: an interrupt can disable or abort a
  // pending breakpoint while its dispatcher is waiting for the guest.
  if (sBreakPointRevision.load(std::memory_order_acquire) != revision ||
      sBreakPointHitRevision.load(std::memory_order_acquire) != revision) return;
  if (sBreakPointCallback != nullptr) {
    const CallbackInterruptScope interrupt;
    const aurora::allocation::ClientAllocationScope clientAllocations{sBreakPointRouting};
    sBreakPointCallback();
  }
}

void process_to(uint64_t target, std::memory_order order) noexcept {
  while (true) {
    const gfx::CommandEpoch epoch;
    if (sAcknowledgedEpoch.load(std::memory_order_acquire) != epoch.value) {
      // No decoder/control mutex and no guest CPU ownership across retirement.
      // Subsequent commands cannot run until old renderer pointers are retired.
      gfx::abandon_recording();
      clear_draw_cache();
      if (!epoch.current()) continue;
      const auto floor = sAbortFloor.load(std::memory_order_acquire);
      if (!epoch.current()) continue;
      sDecoded.store(std::max(floor, sDecoded.load(std::memory_order_acquire)), std::memory_order_release);
      sProcessed.store(std::max(floor, sProcessed.load(std::memory_order_acquire)), std::memory_order_release);
      sAcknowledgedEpoch.store(epoch.value, std::memory_order_release);
      sAcknowledgedEpoch.notify_all();
      sProcessed.notify_all();
      continue;
    }
    ProcessResult result{};
    bool notifyBreakPoint = false;
    uint64_t breakPointRevision = 0;
    uint64_t decoded;
    {
      std::lock_guard execution{sExecutionMutex};
      decoded = sDecoded.load(std::memory_order_relaxed);
      const bool breakpointEnabled = sBreakPoint.enabled &&
          sBreakPoint.revision == sBreakPointRevision.load(std::memory_order_acquire);
      if (breakpointEnabled && decoded == sBreakPoint.cursor) {
        sBreakPointHitRevision.store(sBreakPoint.revision, std::memory_order_release);
        sBreakPoint.hit = true;
        if (sBreakPoint.notified) return;
        sBreakPoint.notified = true;
        notifyBreakPoint = true;
        breakPointRevision = sBreakPoint.revision;
      } else {
        if (decoded >= target) return;
        const uint64_t end = breakpointEnabled ? std::min(target, sBreakPoint.cursor) : target;
        AURORA_ASSERT(end > decoded, "FIFO breakpoint is behind its decoder cursor");
        std::lock_guard buffer{sBufferMutex};
        AURORA_ASSERT(decoded >= sStreamBase && end <= detail::sWritten.load(std::memory_order_acquire),
                      "FIFO processing range [{}, {}) is outside buffered range [{}, {})", decoded, end,
                      sStreamBase, detail::sWritten.load(std::memory_order_relaxed));
        const auto start = static_cast<uint32_t>(decoded - sStreamBase);
        const auto size = static_cast<uint32_t>(end - decoded);
        result = process(detail::sBufferData + start, size, epoch);
        if (!epoch.current()) continue;
        AURORA_ASSERT(result.bytesProcessed > 0 && result.bytesProcessed <= size,
                      "FIFO processor made invalid progress: processed {} of {} remaining bytes", result.bytesProcessed,
                      size);
        decoded += result.bytesProcessed;
        sDecoded.store(decoded, std::memory_order_release);
      }
    }
    if (notifyBreakPoint) {
      dispatch_breakpoint(breakPointRevision);
      continue;
    }
    if (result.drawDone) {
      if (epoch.current()) gfx::complete_draw();
      dispatch_draw_done(epoch);
    }
    if (result.tokenWrite) dispatch_draw_sync(result.token, result.tokenInterrupt, epoch);
    if (!epoch.current()) continue;
    sProcessed.store(decoded, order);
    sProcessed.notify_all();
  }
}

void worker_main(std::stop_token token) noexcept {
  std::stop_callback wakeOnStop{token, wake_worker};
  while (true) {
    const uint32_t event = sWorkerWake.load(std::memory_order_acquire);
    const uint64_t published = sPublished.load(std::memory_order_acquire);
    process_to(published, std::memory_order_release);
    if (sPublished.load(std::memory_order_acquire) != published) {
      continue;
    }

    if (token.stop_requested()) {
      break;
    }
    sWorkerWake.wait(event, std::memory_order_acquire);
  }
}

void start_worker() {
  if (kProcessingMode != ProcessingMode::Thread || sWorkerThread.joinable()) {
    return;
  }
  sWorkerThread = thread::Thread{{
                                     .name = "Aurora FIFO processor",
                                     .affinity = thread::Affinity::SharedCache,
                                 },
                                 worker_main};
}

void stop_worker() {
  if (!sWorkerThread.joinable()) {
    return;
  }
  // Shutdown must not wait forever on a breakpoint whose owner is retiring.
  disable_breakpoint();
  sWorkerThread.request_stop();
  const aurora::os::GuestThreadWaitScope wait;
  sWorkerThread.join();
}
} // namespace

ProcessingMode processing_mode() noexcept { return kProcessingMode; }

void init() {
  const aurora::allocation::HostAllocationScope hostAllocations;
  stop_worker();

  {
    std::lock_guard execution{sExecutionMutex};
    constexpr uint32_t initialCapacity = 64 * 1024;
    free(sAddressBuffer.load(std::memory_order_acquire));
    sAddressBuffer = static_cast<uint8_t*>(malloc(kAddressBufferSize));
    AURORA_ASSERT(sAddressBuffer != nullptr, "fifo::init: failed to allocate FIFO address space");
    free(detail::sBufferData);
    detail::sBufferData = static_cast<uint8_t*>(malloc(initialCapacity));
    AURORA_ASSERT(detail::sBufferData != nullptr, "fifo::init: failed to allocate {} bytes", initialCapacity);
    detail::sBufferSize = 0;
    detail::sBufferCapacity = initialCapacity;
    detail::sInDisplayList = false;
    detail::sDlBuffer = nullptr;
    detail::sDlSize = 0;
    detail::sDlWritePos = 0;

    sFrameActive = false;
    sPendingDraws = 0;
    sStreamBase = 0;
    detail::sWritten.store(0, std::memory_order_relaxed);
    sPublished.store(0, std::memory_order_relaxed);
    sDecoded.store(0, std::memory_order_relaxed);
    sProcessed.store(0, std::memory_order_relaxed);
    sWorkerWake.store(0, std::memory_order_relaxed);
    sAbortFloor.store(0, std::memory_order_relaxed);
    sAcknowledgedEpoch.store(gfx::CommandEpoch{}.value, std::memory_order_relaxed);
    ++sGeneration;
    sActive = true;
    sBreakPoint = {};
  }

  start_worker();
}

void shutdown() {
  const aurora::allocation::HostAllocationScope hostAllocations;
  stop_worker();
  {
    std::lock_guard execution{sExecutionMutex};
    sActive = false;
    free(sAddressBuffer.load(std::memory_order_acquire));
    sAddressBuffer = nullptr;
  }
  clear_draw_cache();
}

void begin_frame() noexcept { sFrameActive = true; }

void end_frame() noexcept {
  const aurora::allocation::HostAllocationScope hostAllocations;
  sFrameActive = false;
  clear_draw_cache(); // command_processor
}

void write_data_grow(const void* data, uint32_t length) {
  const aurora::allocation::HostAllocationScope hostAllocations;
  const uint64_t needed64 = static_cast<uint64_t>(detail::sBufferSize) + length;
  AURORA_ASSERT(needed64 <= std::numeric_limits<uint32_t>::max(), "fifo::write_data: buffer size overflow");
  const auto needed = static_cast<uint32_t>(needed64);
  const auto doubledCapacity = static_cast<uint64_t>(detail::sBufferCapacity) * 2;
  const auto newCapacity = static_cast<uint32_t>(
      std::min<uint64_t>(std::max(doubledCapacity, needed64), std::numeric_limits<uint32_t>::max()));
  const auto grow = [newCapacity] {
    auto* resized = static_cast<uint8_t*>(realloc(detail::sBufferData, newCapacity));
    AURORA_ASSERT(resized != nullptr, "fifo::write_data: failed to allocate {} bytes", newCapacity);
    detail::sBufferData = resized;
    detail::sBufferCapacity = newCapacity;
  };
  if (sWorkerThread.joinable()) {
    std::lock_guard lock{sBufferMutex};
    grow();
  } else {
    grow();
  }
  std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
  detail::sBufferSize = needed;
  detail::sWritten.fetch_add(length, std::memory_order_release);
}

void publish() noexcept {
  const aurora::allocation::HostAllocationScope hostAllocations;
  if (!sFrameActive || kProcessingMode == ProcessingMode::Drain || detail::sInDisplayList) {
    return;
  }

  const uint64_t target = sStreamBase + detail::sBufferSize;
  if (target > sPublished.load(std::memory_order_relaxed)) {
    sPendingDraws = 0;
    sPublished.store(target, std::memory_order_release);
    if (kProcessingMode == ProcessingMode::Thread) {
      wake_worker();
    } else {
      process_to(target, std::memory_order_relaxed);
    }
  }
}

DrawDoneCallback set_draw_done_callback(DrawDoneCallback callback) noexcept {
  const CallbackLock lock;
  sDrawDoneRouting = aurora::allocation::routing_state;
  return std::exchange(sDrawDoneCallback, callback);
}

DrawSyncCallback set_draw_sync_callback(DrawSyncCallback callback) noexcept {
  const CallbackLock lock;
  sDrawSyncRouting = aurora::allocation::routing_state;
  return std::exchange(sDrawSyncCallback, callback);
}

BreakPointCallback set_breakpoint_callback(BreakPointCallback callback) noexcept {
  const CallbackLock lock;
  sBreakPointRouting = aurora::allocation::routing_state;
  return std::exchange(sBreakPointCallback, callback);
}

void enable_breakpoint(uint64_t generation, uint64_t readOrigin, uint32_t initialReadOffset,
                       uint32_t ringSize, uint32_t breakOffset) noexcept {
  {
    std::unique_lock execution{sExecutionMutex, std::defer_lock};
    while (!execution.try_lock()) {
      // Do not restore CPU ownership while holding the decoder lock.
      const aurora::os::GuestThreadWaitScope wait;
      std::this_thread::yield();
    }
    AURORA_ASSERT(sActive && sGeneration == generation, "GX breakpoint refers to a retired FIFO");
    AURORA_ASSERT(ringSize != 0 && initialReadOffset < ringSize && breakOffset < ringSize,
                  "GX breakpoint is outside its GP FIFO");
    const uint64_t decoded = sDecoded.load(std::memory_order_relaxed);
    AURORA_ASSERT(decoded >= readOrigin, "GX breakpoint FIFO origin is after its read cursor");
    const uint64_t offset = (decoded - readOrigin + initialReadOffset) % ringSize;
    const uint64_t distance = (static_cast<uint64_t>(breakOffset) + ringSize - offset) % ringSize;
    sBreakPoint = {.cursor = decoded + distance, .revision = ++sBreakPointRevision, .enabled = true};
  }
  wake_worker();
}

void disable_breakpoint() noexcept {
  // The alarm watchdog runs with scheduling disabled. Invalidating delivery
  // must never wait for the decoder or a callback that needs the guest CPU.
  sBreakPointRevision.fetch_add(1, std::memory_order_acq_rel);
  wake_worker();
}

void abort_frame() noexcept {
  const auto floor = detail::sWritten.load(std::memory_order_acquire);
  sAbortFloor.store(floor, std::memory_order_release);
  disable_breakpoint();
  gfx::abandon_command_epoch();
  auto published = sPublished.load(std::memory_order_relaxed);
  while (published < floor && !sPublished.compare_exchange_weak(published, floor, std::memory_order_release)) {}
  wake_worker();
}

uint16_t read_draw_sync() noexcept { return sDrawSyncToken.load(std::memory_order_acquire); }

bool peek_draw_sync_z(uint16_t x, uint16_t y, uint32_t& z) {
  if (sCallbackEfbRead == nullptr) return false;
  auto& snapshot = sCallbackEfbRead->snapshot;
  if (snapshot == AURORA_INVALID_DEPTH_SNAPSHOT_ID) {
    snapshot = gfx::depth_peek::capture_efb();
  }
  AURORA_ASSERT(gfx::depth_peek::read_snapshot(snapshot, x, y, z),
                "GX draw-sync EFB readback is unavailable or coordinates are outside the EFB");
  return true;
}

void finish_draw() noexcept {
  if (!sFrameActive || kProcessingMode == ProcessingMode::Drain || detail::sInDisplayList) {
    return;
  }
  if (++sPendingDraws >= kDrawBatchSize) {
    publish();
  }
}

void patch_u32(uint32_t offset, uint32_t val) {
  AURORA_ASSERT(!detail::sInDisplayList && offset <= detail::sBufferSize &&
                    sizeof(uint32_t) <= detail::sBufferSize - offset,
                "fifo::patch_u32: invalid patch offset {} (buffer size {})", offset, detail::sBufferSize);
  AURORA_ASSERT(sStreamBase + offset >= sPublished.load(std::memory_order_relaxed),
                "fifo::patch_u32: offset {} is below the published watermark", offset);
  const auto out = bswap(val);
  std::memcpy(detail::sBufferData + offset, &out, sizeof(out));
}

void begin_display_list(uint8_t* buf, uint32_t size) {
  detail::sInDisplayList = true;
  detail::sDlBuffer = buf;
  detail::sDlSize = size;
  detail::sDlWritePos = 0;
}

uint32_t end_display_list() {
  detail::sInDisplayList = false;
  const uint32_t bytesWritten = detail::sDlWritePos;
  const uint32_t padded = (bytesWritten + 31) & ~31u;
  while (detail::sDlWritePos < padded && detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = 0;
  }
  detail::sDlBuffer = nullptr;
  detail::sDlSize = 0;
  detail::sDlWritePos = 0;
  return padded;
}

bool in_display_list() { return detail::sInDisplayList; }

void drain() {
  const aurora::allocation::HostAllocationScope hostAllocations;
  if (detail::sBufferSize == 0 &&
      sAcknowledgedEpoch.load(std::memory_order_acquire) == gfx::CommandEpoch{}.value) return;

  ZoneScoped;
  const uint64_t target = sStreamBase + detail::sBufferSize;

  switch (kProcessingMode) {
  case ProcessingMode::Drain:
  case ProcessingMode::Inline:
    sPublished.store(target, std::memory_order_relaxed);
    process_to(target, std::memory_order_relaxed);
    break;
  case ProcessingMode::Thread: {
    sPublished.store(target, std::memory_order_release);
    wake_worker();

    auto acknowledged = sAcknowledgedEpoch.load(std::memory_order_acquire);
    if (acknowledged != gfx::CommandEpoch{}.value) {
      const aurora::os::GuestThreadWaitScope wait;
      do {
        sAcknowledgedEpoch.wait(acknowledged, std::memory_order_acquire);
        acknowledged = sAcknowledgedEpoch.load(std::memory_order_acquire);
      } while (acknowledged != gfx::CommandEpoch{}.value);
    }
    uint64_t processed = sProcessed.load(std::memory_order_acquire);
    if (processed < target) {
      const aurora::os::GuestThreadWaitScope wait;
      do {
        sProcessed.wait(processed, std::memory_order_acquire);
        processed = sProcessed.load(std::memory_order_acquire);
      } while (processed < target);
    }
    break;
  }
  }

  {
    std::lock_guard lock{sBufferMutex};
    sStreamBase = target;
    detail::sBufferSize = 0;
  }
  sPendingDraws = 0;
}

const uint8_t* get_buffer_data() { return detail::sBufferData; }
uint32_t get_buffer_size() { return detail::sBufferSize; }

CursorSnapshot cursor_snapshot() {
  const auto completed = sProcessed.load(std::memory_order_acquire);
  const auto consumed = sDecoded.load(std::memory_order_acquire);
  const auto published = sPublished.load(std::memory_order_acquire);
  const auto written = detail::sWritten.load(std::memory_order_acquire);
  return {sAddressBuffer.load(std::memory_order_acquire), kAddressBufferSize, written,
          published, consumed, completed,
          sGeneration.load(std::memory_order_acquire), sActive.load(std::memory_order_acquire),
          sBreakPointHitRevision.load(std::memory_order_acquire) == sBreakPointRevision.load(std::memory_order_acquire)};
}

void clear_buffer() {
  const uint64_t processed = sProcessed.load(std::memory_order_acquire);
  AURORA_ASSERT(sPublished.load(std::memory_order_acquire) == processed,
                "fifo::clear_buffer: published commands are still pending");
  std::lock_guard lock{sBufferMutex};
  sStreamBase = processed;
  detail::sBufferSize = 0;
  detail::sWritten.store(processed, std::memory_order_release);
  sPendingDraws = 0;
}

} // namespace aurora::gx::fifo
