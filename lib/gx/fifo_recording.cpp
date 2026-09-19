#include "fifo_recording.hpp"

#include "fifo.hpp"
#include "../gfx/command_epoch.hpp"
#include <aurora/allocation.hpp>
#include <aurora/guest_thread.hpp>

#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace aurora::gx::fifo {
namespace detail {
std::atomic<bool> sRecordingActive{false};
std::atomic<bool> sRecordingSubmitting{false};
} // namespace detail

namespace {
constexpr Module Log{"aurora::gx::fifo::recording"};
std::vector<uint8_t> sBytes;
detail::CommandStream* sOwner = nullptr;
uint64_t sGeneration = 0;
uint64_t sRevision = 0;
gfx::CommandEpoch sEpoch;
} // namespace

void begin_patchable_recording() {
  AURORA_ASSERT(!detail::recording_pending(), "Nested patchable GX command recording");
  AURORA_ASSERT(!in_display_list(), "Patchable GX command inside display-list recording");
  sOwner = detail::sCPUStream.load(std::memory_order_acquire);
  AURORA_ASSERT(sOwner != nullptr, "Patchable GX command requires an attached CPU FIFO");
  sGeneration = sOwner->id;
  sRevision = sOwner->revision.load(std::memory_order_acquire);
  sEpoch = gfx::CommandEpoch{};
  sBytes.clear();
  detail::sRecordingActive.store(true, std::memory_order_release);
}

bool end_patchable_recording(void (*onSubmitted)()) {
  const aurora::allocation::HostAllocationScope hostAllocations;
  AURORA_ASSERT(detail::recording_active(), "No patchable GX command recording to finish");
  const auto epoch = sEpoch;
  AURORA_ASSERT(detail::sCPUStream.load(std::memory_order_acquire) == sOwner &&
                    sOwner->id == sGeneration && sOwner->revision.load(std::memory_order_acquire) == sRevision,
                "Patchable GX command FIFO changed before submission");
  // Clear recording before enqueueing so normal FIFO writes and their capacity
  // checks apply. Keep the source alive across producer suspension, and prevent
  // rebinding from splitting this immutable command across two FIFO owners.
  auto bytes = std::move(sBytes);
  detail::sRecordingSubmitting.store(true, std::memory_order_release);
  detail::sRecordingActive.store(false, std::memory_order_release);
  struct SubmissionScope {
    ~SubmissionScope() {
      detail::sRecordingSubmitting.store(false, std::memory_order_release);
      detail::sRecordingSubmitting.notify_all();
    }
  } submission;
  if (!epoch.current()) return false;
  write_data(bytes.data(), static_cast<uint32_t>(bytes.size()));
  if (!epoch.current()) return false;
  if (onSubmitted) onSubmitted();
  return true;
}

void detail::append_recording(const void* data, uint32_t length) {
  const aurora::allocation::HostAllocationScope hostAllocations;
  AURORA_ASSERT(recording_active(), "GX command append outside patchable recording");
  AURORA_ASSERT(length <= std::numeric_limits<uint32_t>::max() - sBytes.size(),
                "Patchable GX command exceeds its encoded size limit");
  if (length == 0) return;
  const auto* bytes = static_cast<const uint8_t*>(data);
  sBytes.insert(sBytes.end(), bytes, bytes + length);
}

uint32_t detail::recording_size() { return static_cast<uint32_t>(sBytes.size()); }
const uint8_t* detail::recording_data() { return sBytes.data(); }

void detail::patch_recording_u32(uint32_t offset, uint32_t value) {
  AURORA_ASSERT(recording_active() && offset <= sBytes.size() && sizeof(value) <= sBytes.size() - offset,
                "Patchable GX command has an invalid patch offset");
  const auto encoded = bswap(value);
  std::memcpy(sBytes.data() + offset, &encoded, sizeof(encoded));
}

void detail::discard_recording() {
  const aurora::allocation::HostAllocationScope hostAllocations;
  if (sRecordingSubmitting.load(std::memory_order_acquire)) {
    const aurora::os::GuestThreadWaitScope wait;
    while (sRecordingSubmitting.load(std::memory_order_acquire))
      sRecordingSubmitting.wait(true, std::memory_order_acquire);
  }
  sRecordingActive.store(false, std::memory_order_release);
  sOwner = nullptr;
  std::vector<uint8_t>{}.swap(sBytes);
}
} // namespace aurora::gx::fifo
