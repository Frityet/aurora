#include "gx.hpp"

#include "../../gx/fifo.hpp"

#include <cstring>
#include <limits>
#include <aurora/guest_thread.hpp>
#include <mutex>
#include <thread>
#include <type_traits>

namespace {
constexpr u32 kFifoMagic = 0x4155464f; // Native opaque GXFifoObj record.

struct FifoRecord {
  u32 magic = kFifoMagic;
  u32 size = 0;
  uintptr_t base = 0;
  uintptr_t read = 0;
  uintptr_t write = 0;
  u32 highWatermark = 0;
  u32 lowWatermark = 0;
  u32 count = 0;
  bool nativeBuffer = false;
  bool customLimits = false;
  bool cpuBound = false;
  bool gpBound = false;
  bool wrap = false;
};
static_assert(std::is_trivially_copyable_v<FifoRecord>);
static_assert(sizeof(FifoRecord) <= sizeof(GXFifoObj));

struct FifoBinding {
  FifoRecord record;
  uint64_t generation = 0;
  bool ready = false;
};

FifoBinding sCPUFifo;
FifoBinding sGPFifo;
std::mutex sFifoMutex;
class FifoLock {
public:
  FifoLock() : lock(sFifoMutex, std::defer_lock) {
    while (!lock.try_lock()) {
      const aurora::os::GuestThreadWaitScope wait;
      std::this_thread::yield();
    }
  }
private:
  std::unique_lock<std::mutex> lock;
};
using CursorSnapshot = aurora::gx::fifo::CursorSnapshot;

FifoRecord read_record(const GXFifoObj* fifo) {
  AURORA_ASSERT(fifo != nullptr, "GX FIFO object is null");
  FifoRecord record;
  // GXFifoObj has byte alignment; do not alias it as a native pointer record.
  std::memcpy(&record, fifo, sizeof(record));
  AURORA_ASSERT(record.magic == kFifoMagic && record.size != 0,
                "GX FIFO object has not been initialized");
  return record;
}

void write_record(GXFifoObj* fifo, const FifoRecord& record) {
  AURORA_ASSERT(fifo != nullptr, "GX FIFO output object is null");
  std::memset(fifo, 0, sizeof(*fifo));
  std::memcpy(fifo, &record, sizeof(record));
}

void set_default_limits(FifoRecord& record) {
  record.highWatermark = record.size > 16 * 1024 ? record.size - 16 * 1024 : record.size;
  record.lowWatermark = (record.size / 2) & ~31u;
}

bool ready(const FifoBinding& binding, const CursorSnapshot& cursor) {
  return binding.ready && cursor.active && binding.generation == cursor.generation;
}

bool same_fifo(const FifoRecord& first, const FifoRecord& second) {
  if (first.nativeBuffer || second.nativeBuffer) {
    return first.nativeBuffer && second.nativeBuffer;
  }
  return first.base == second.base && first.size == second.size;
}

FifoRecord snapshot(const FifoBinding& binding, const CursorSnapshot& cursor) {
  FifoRecord record = binding.record;
  record.cpuBound = ready(sCPUFifo, cursor) && same_fifo(record, sCPUFifo.record);
  record.gpBound = ready(sGPFifo, cursor) && same_fifo(record, sGPFifo.record);
  AURORA_ASSERT(cursor.consumed <= cursor.written, "GX FIFO consumer passed its producer");
  const uint64_t count = cursor.written - cursor.consumed;
  AURORA_ASSERT(count <= std::numeric_limits<u32>::max(), "GX FIFO byte count exceeds its API width");
  record.count = static_cast<u32>(count);
  // Both default and caller-supplied FIFOs use a stable logical ring over the
  // decoder stream. A saved address survives growth and reclamation of decoded
  // command storage. Counts retain actual outstanding bytes, including overflow.
  const uint64_t produced = cursor.written + cursor.initialWriteOffset;
  const uint64_t consumed = cursor.consumed + cursor.initialReadOffset;
  record.write = record.base + produced % record.size;
  record.read = record.base + consumed % record.size;
  record.wrap = produced >= record.size;
  return record;
}

void bind_fifo(FifoBinding& binding, const GXFifoObj* fifo, bool cpu) {
  const auto bind = cpu ? aurora::gx::fifo::bind_cpu_fifo : aurora::gx::fifo::bind_gpu_fifo;
  if (fifo == nullptr) {
    bind(nullptr, 0, 0, 0, 0);
    binding = {};
    return;
  }
  const auto record = read_record(fifo);
  if (record.nativeBuffer) {
    const auto initial = aurora::gx::fifo::default_cursor_snapshot();
    AURORA_ASSERT(initial.active && record.base == reinterpret_cast<uintptr_t>(initial.addressBase) &&
                  record.size == initial.addressSize, "GX FIFO attachment refers to a retired default address space");
  }
  const auto id = bind(reinterpret_cast<void*>(record.base), record.size,
                       static_cast<uint32_t>(record.read - record.base),
                       static_cast<uint32_t>(record.write - record.base), record.count);
  binding = {record, id, true};
}
} // namespace

extern "C" {

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size) {
  FifoRecord record;
  if (base == nullptr && size == 0) {
    const auto cursor = aurora::gx::fifo::default_cursor_snapshot();
    AURORA_ASSERT(cursor.active && cursor.addressBase != nullptr && cursor.addressSize != 0,
                  "GXInit requires an initialized Aurora command stream");
    record.nativeBuffer = true;
    record.base = reinterpret_cast<uintptr_t>(cursor.addressBase);
    record.size = cursor.addressSize;
  } else {
    AURORA_ASSERT(base != nullptr && size != 0 &&
                      reinterpret_cast<uintptr_t>(base) <= std::numeric_limits<uintptr_t>::max() - size,
                  "GXInitFifoBase received an invalid buffer range");
    record.base = reinterpret_cast<uintptr_t>(base);
    record.size = size;
  }
  record.read = record.write = record.base;
  set_default_limits(record);
  write_record(fifo, record);
}

void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr) {
  auto record = read_record(fifo);
  const auto read = reinterpret_cast<uintptr_t>(readPtr);
  const auto write = reinterpret_cast<uintptr_t>(writePtr);
  AURORA_ASSERT(read >= record.base && read - record.base < record.size &&
                    write >= record.base && write - record.base < record.size,
                "GXInitFifoPtrs requires pointers within the FIFO buffer");
  record.read = read;
  record.write = write;
  record.count = static_cast<u32>(write >= read ? write - read : record.size - (read - write));
  write_record(fifo, record);
}

void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWaterMark, u32 loWaterMark) {
  auto record = read_record(fifo);
  AURORA_ASSERT(loWaterMark <= hiWaterMark && hiWaterMark <= record.size,
                "GX FIFO watermarks exceed its buffer range");
  record.highWatermark = hiWaterMark;
  record.lowWatermark = loWaterMark;
  record.customLimits = true;
  write_record(fifo, record);
}

void GXSetCPUFifo(const GXFifoObj* fifo) {
  const FifoLock lock;
  bind_fifo(sCPUFifo, fifo, true);
}

void GXSetGPFifo(const GXFifoObj* fifo) {
  const FifoLock lock;
  bind_fifo(sGPFifo, fifo, false);
}

GXBool GXGetCPUFifo(GXFifoObj* fifo) {
  const FifoLock lock;
  const auto cursor = aurora::gx::fifo::cursor_snapshot(sCPUFifo.generation);
  if (!ready(sCPUFifo, cursor)) return GX_FALSE;
  write_record(fifo, snapshot(sCPUFifo, cursor));
  return GX_TRUE;
}

GXBool GXGetGPFifo(GXFifoObj* fifo) {
  const FifoLock lock;
  const auto cursor = aurora::gx::fifo::cursor_snapshot(sGPFifo.generation);
  if (!ready(sGPFifo, cursor)) return GX_FALSE;
  write_record(fifo, snapshot(sGPFifo, cursor));
  return GX_TRUE;
}

void GXSaveCPUFifo(GXFifoObj* fifo) {
  AURORA_ASSERT(GXGetCPUFifo(fifo), "GXSaveCPUFifo requires an attached CPU FIFO");
}

void GXGetFifoPtrs(const GXFifoObj* fifo, void** readPtr, void** writePtr) {
  const auto record = read_record(fifo);
  *readPtr = reinterpret_cast<void*>(record.read);
  *writePtr = reinterpret_cast<void*>(record.write);
}

void* GXGetFifoBase(const GXFifoObj* fifo) { return reinterpret_cast<void*>(read_record(fifo).base); }
u32 GXGetFifoSize(const GXFifoObj* fifo) { return read_record(fifo).size; }
u32 GXGetFifoCount(const GXFifoObj* fifo) { return read_record(fifo).count; }
GXBool GXGetFifoWrap(const GXFifoObj* fifo) { return read_record(fifo).wrap; }

void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underlow, u32* fifoCount,
                     GXBool* cpu_write, GXBool* gp_read, GXBool* fifowrap) {
  const auto record = read_record(fifo);
  *overhi = record.count > record.highWatermark;
  *underlow = record.count < record.lowWatermark;
  *fifoCount = record.count;
  *cpu_write = record.cpuBound;
  *gp_read = record.gpBound;
  *fifowrap = record.wrap;
}

void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt) {
  const FifoLock lock;
  const auto cursor = aurora::gx::fifo::cursor_snapshot(sGPFifo.generation);
  AURORA_ASSERT(ready(sGPFifo, cursor), "GXGetGPStatus requires an attached GP FIFO");
  const auto record = snapshot(sGPFifo, cursor);
  *overhi = record.count > record.highWatermark;
  *underlow = record.count < record.lowWatermark;
  *readIdle = cursor.consumed == cursor.published;
  *cmdIdle = cursor.breakpoint || cursor.consumed == cursor.published;
  // A stopped command decoder does not imply submitted Metal/GPU work is idle.
  *brkpt = cursor.breakpoint;
}

GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback callback) {
  return aurora::gx::fifo::set_breakpoint_callback(callback);
}

void GXEnableBreakPt(void* breakPt) {
  FifoBinding binding;
  CursorSnapshot cursor;
  {
    const FifoLock lock;
    cursor = aurora::gx::fifo::cursor_snapshot(sGPFifo.generation);
    AURORA_ASSERT(ready(sGPFifo, cursor), "GXEnableBreakPt requires an attached GP FIFO");
    binding = sGPFifo;
  }
  const auto& record = binding.record;
  const auto address = reinterpret_cast<uintptr_t>(breakPt);
  AURORA_ASSERT(address >= record.base && address - record.base < record.size,
                "GXEnableBreakPt requires an address within the attached GP FIFO");
  aurora::gx::fifo::enable_breakpoint(cursor.generation, 0, cursor.initialReadOffset, record.size,
                                     static_cast<u32>(address - record.base));
}

void GXDisableBreakPt() { aurora::gx::fifo::disable_breakpoint(); }

// Pending encoded commands belong to FIFO storage, not to its current binding.
}
