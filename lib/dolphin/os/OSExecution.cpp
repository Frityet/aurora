#include <dolphin/os.h>
#include <dolphin/os/OSThread.h>

#include <bit>
#include <mutex>
#include <thread>

namespace {

// Cooperative emulated-CPU ownership. This gate serializes callers of these
// SDK APIs; it does not suspend native workers or mask host signals. Interrupt
// state belongs to the calling context, while Reschedule is CPU-global as on
// the original SDK. Only the owning host thread ever unlocks the mutex.
std::mutex sCpuGate;
thread_local bool sOwnsCpu = false;
thread_local bool sInterruptsEnabled = true;
u32 sReschedule = 0;

s32 scheduler_count() { return std::bit_cast<s32>(sReschedule); }

void acquire_cpu() {
  if (!sOwnsCpu) {
    sCpuGate.lock();
    sOwnsCpu = true;
  }
}

void release_cpu() {
  sOwnsCpu = false;
  sCpuGate.unlock();
}

void release_cpu_if_enabled() {
  if (sInterruptsEnabled && scheduler_count() <= 0) {
    release_cpu();
  }
}

BOOL set_interrupts(bool enabled) {
  acquire_cpu();
  const BOOL previous = sInterruptsEnabled;
  sInterruptsEnabled = enabled;
  release_cpu_if_enabled();
  return previous;
}

} // namespace

BOOL OSDisableInterrupts() { return set_interrupts(false); }

BOOL OSEnableInterrupts() { return set_interrupts(true); }

BOOL OSRestoreInterrupts(BOOL level) { return set_interrupts(level != FALSE); }

s32 OSDisableScheduler() {
  const BOOL enabled = OSDisableInterrupts();
  const s32 count = scheduler_count();
  // Unsigned storage preserves the PowerPC addi wrap without signed C++ UB.
  ++sReschedule;
  OSRestoreInterrupts(enabled);
  return count;
}

s32 OSEnableScheduler() {
  const BOOL enabled = OSDisableInterrupts();
  const s32 count = scheduler_count();
  --sReschedule;
  OSRestoreInterrupts(enabled);
  return count;
}

void OSYieldThread() {
  const BOOL enabled = OSDisableInterrupts();
  if (scheduler_count() <= 0) {
    // SelectThread(TRUE) can switch even when the caller's saved interrupt
    // state is disabled. Retain that context's bit across the explicit yield.
    release_cpu();
    std::this_thread::yield();
    acquire_cpu();
  }
  OSRestoreInterrupts(enabled);
}
