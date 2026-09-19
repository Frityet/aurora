#pragma once

#include <atomic>
#include <cstdint>

namespace aurora::gx::fifo {

// Aurora commands whose payload length is unknown until their closing API
// call are assembled outside the hardware ring, then submitted immutably.
void begin_patchable_recording();
bool end_patchable_recording(void (*onSubmitted)() = nullptr);

namespace detail {
extern std::atomic<bool> sRecordingActive;
extern std::atomic<bool> sRecordingSubmitting;
inline bool recording_active() noexcept { return sRecordingActive.load(std::memory_order_acquire); }
inline bool recording_pending() noexcept {
  return recording_active() || sRecordingSubmitting.load(std::memory_order_acquire);
}
void append_recording(const void* data, uint32_t length);
uint32_t recording_size();
const uint8_t* recording_data();
void patch_recording_u32(uint32_t offset, uint32_t value);
void discard_recording();
} // namespace detail
} // namespace aurora::gx::fifo
