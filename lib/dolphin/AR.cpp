#include "AR.hpp"
#include <dolphin/ar.h>
#include <dolphin/os.h>
#include <dolphin/os/OSArena.h>
#include <aurora/allocation.hpp>
#include <aurora/exception.hpp>
#include <aurora/guest_thread.hpp>

#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace {
constexpr u32 aramStart = 0x4000;
u8* aramBase;
u32 aramSize;
u32 allocationTop;
u32* allocationLengths;
u32 allocationCapacity;
u32 allocationCount;
bool arqInitialized;
ARCallback dmaCallback;
aurora::allocation::RoutingState dmaRouting;

// Callbacks may wait in SDK queues. Their setter/reset must release the guest
// CPU while waiting for the callback to finish, as other SDK mutex users do.
OSMutex callbackMutex{};
struct CallbackLock {
  CallbackLock() { OSLockMutex(&callbackMutex); }
  ~CallbackLock() { OSUnlockMutex(&callbackMutex); }
};

[[noreturn]] void invalid_aram(const char* message) {
  aurora::throw_host_exception<std::out_of_range>(message);
}

u8* resolve_aram(std::uintptr_t offset, u32 length) {
  if (aramBase == nullptr || offset > aramSize || length > aramSize - offset) {
    invalid_aram("ARAM transfer exceeds its retained MEM2 range");
  }
  return aramBase + offset;
}

void reset_aram() {
  aramBase = nullptr;
  aramSize = allocationTop = allocationCapacity = allocationCount = 0;
  allocationLengths = nullptr;
  arqInitialized = false;
  dmaCallback = nullptr;
  dmaRouting = {};
}

void copy_aram(u32 type, std::uintptr_t source, std::uintptr_t destination, u32 length) {
  if (type == ARAM_DIR_MRAM_TO_ARAM) {
    auto* target = resolve_aram(destination, length);
    if (source == 0 && length != 0) invalid_aram("ARAM source is null");
    if (length != 0) {
      auto* input = reinterpret_cast<void*>(source);
      DCInvalidateRange(input, length);
      std::memcpy(target, input, length);
      DCFlushRange(target, length);
    }
  } else if (type == ARAM_DIR_ARAM_TO_MRAM) {
    auto* input = resolve_aram(source, length);
    if (destination == 0 && length != 0) invalid_aram("ARAM destination is null");
    if (length != 0) {
      auto* target = reinterpret_cast<void*>(destination);
      DCFlushRange(input, length);
      std::memcpy(target, input, length);
      DCFlushRange(target, length);
    }
  } else {
    aurora::throw_host_exception<std::invalid_argument>("Unknown ARAM transfer direction");
  }
}
}

u32 ARInit(u32* stackIndex, u32 numEntries) {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock callback;
  if (aramBase != nullptr) return aramStart;
  auto* low = static_cast<u8*>(OSGetMEM2ArenaLo());
  auto* high = static_cast<u8*>(OSGetMEM2ArenaHi());
  const auto begin = reinterpret_cast<std::uintptr_t>(low);
  const auto end = reinterpret_cast<std::uintptr_t>(high);
  if (begin == 0 || end < begin || end - begin < aramStart || end - begin > std::numeric_limits<u32>::max()) {
    aurora::throw_host_exception<std::logic_error>("ARInit requires the original retained MEM2 arena");
  }
  aramBase = low;
  aramSize = static_cast<u32>(end - begin);
  allocationTop = aramStart;
  allocationLengths = stackIndex;
  allocationCapacity = numEntries;
  allocationCount = 0;
  dmaCallback = nullptr;
  dmaRouting = {};
  return aramStart;
}

u32 ARAlloc(u32 length) {
  const aurora::os::GuestThreadExecutionScope execution;
  if (aramBase == nullptr) invalid_aram("ARAlloc requires initialized ARAM");
  if (allocationLengths != nullptr && allocationCount >= allocationCapacity) {
    invalid_aram("ARAlloc exceeds its caller-owned allocation stack");
  }
  const u32 offset = allocationTop;
  // RVL aralt performs 32-bit address accounting without a capacity check.
  // Actual memory access is validated separately by the transfer boundary.
  allocationTop += length;
  if (allocationLengths != nullptr) allocationLengths[allocationCount++] = length;
  return offset;
}

u32 ARFree(u32* length) {
  const aurora::os::GuestThreadExecutionScope execution;
  if (aramBase == nullptr || allocationLengths == nullptr || allocationCount == 0) {
    invalid_aram("ARFree has no caller-owned allocation stack entry");
  }
  const auto released = allocationLengths[--allocationCount];
  allocationTop -= released;
  if (length != nullptr) *length = released;
  return allocationTop;
}

BOOL ARCheckInit() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aramBase != nullptr;
}

void ARReset() {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock callback;
  reset_aram();
}

// Called by MEM2's explicit owner before its borrowed storage is released.
namespace aurora {
void release_aram_mem2_owner(void* memory, std::size_t size) {
  const os::GuestThreadExecutionScope execution;
  const CallbackLock callback;
  const auto begin = reinterpret_cast<std::uintptr_t>(memory);
  const auto base = reinterpret_cast<std::uintptr_t>(aramBase);
  if (aramBase != nullptr && base >= begin && base - begin <= size && aramSize <= size - (base - begin)) {
    reset_aram();
  }
}
}

u32 ARGetBaseAddress() { return aramStart; }
u32 ARGetSize() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aramSize;
}
u32 ARGetInternalSize() { return ARGetSize(); }
void* ARGetStorageAddress() {
  const aurora::os::GuestThreadExecutionScope execution;
  return aramBase;
}

ARCallback ARRegisterDMACallback(ARCallback callback) {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  dmaRouting = aurora::allocation::routing_state;
  return std::exchange(dmaCallback, callback);
}

void ARStartDMA(u32 type, std::uintptr_t source, std::uintptr_t destination, u32 length) {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  copy_aram(type, source, destination, length);
  if (dmaCallback != nullptr) {
    const aurora::allocation::ClientAllocationScope allocations(dmaRouting);
    dmaCallback();
  }
}

u32 ARGetDMAStatus() {
  // The Wii aralt copy has completed before ARStartDMA returns or invokes its
  // callback. There is no independently running hardware DMA in this API.
  return 0;
}

void ARQInit() {
  const aurora::os::GuestThreadExecutionScope execution;
  arqInitialized = true;
}
BOOL ARQCheckInit() {
  const aurora::os::GuestThreadExecutionScope execution;
  return arqInitialized;
}
void ARQReset() {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  arqInitialized = false;
}

void ARQPostRequest(ARQRequest* request, u32 owner, u32 type, u32 priority, std::uintptr_t source,
                    std::uintptr_t destination, u32 length, ARQCallback callback) {
  const aurora::os::GuestThreadExecutionScope execution;
  const CallbackLock lock;
  if (!arqInitialized || request == nullptr) {
    aurora::throw_host_exception<std::logic_error>("ARQ requires its initialized SDK queue and an actual request");
  }
  request->next = nullptr;
  request->owner = owner;
  request->type = type;
  request->priority = priority;
  request->source = source;
  request->dest = destination;
  request->length = length;
  request->callback = callback;
  // Completion is reported only after the real copy succeeds. Invalid ranges
  // cannot masquerade as completed requests with an untouched destination.
  copy_aram(type, source, destination, length);
  if (callback != nullptr) {
    const aurora::allocation::ClientAllocationScope allocations;
    callback(reinterpret_cast<std::uintptr_t>(request));
  }
}
