#pragma once

#include "../internal.hpp"

#include <atomic>
#include <cstring>

namespace aurora::gx::fifo {

namespace detail {
extern uint8_t* sBufferData;
extern uint32_t sBufferSize;
extern uint32_t sBufferCapacity;
extern std::atomic<uint64_t> sWritten;
extern bool sInDisplayList;
extern uint8_t* sDlBuffer;
extern uint32_t sDlSize;
extern uint32_t sDlWritePos;
} // namespace detail

enum class ProcessingMode : uint8_t {
  // Process FIFO synchronously at drain()
  Drain,
  // Process FIFO synchronously at publish() (useful for profiling)
  Inline,
  // Process FIFO asynchronously on a worker thread; drain() synchronizes
  Thread
};
ProcessingMode processing_mode() noexcept;

void init();
void shutdown();

void begin_frame() noexcept;
void end_frame() noexcept;

// Out-of-line slow path: grows internal buffer then appends data
void write_data_grow(const void* data, uint32_t length);

inline void write_data(const void* data, const uint32_t length) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (length <= detail::sBufferCapacity - detail::sBufferSize)
        LIKELY {
          std::memcpy(detail::sBufferData + detail::sBufferSize, data, length);
          detail::sBufferSize += length;
          detail::sWritten.fetch_add(length, std::memory_order_release);
          return;
        }
      write_data_grow(data, length);
    }
  else if (length <= detail::sDlSize - detail::sDlWritePos) {
    std::memcpy(detail::sDlBuffer + detail::sDlWritePos, data, length);
    detail::sDlWritePos += length;
  }
}

inline void write_u8(const uint8_t val) {
  if (!detail::sInDisplayList)
    LIKELY {
      if (detail::sBufferSize < detail::sBufferCapacity)
        LIKELY {
          detail::sBufferData[detail::sBufferSize++] = val;
          detail::sWritten.fetch_add(1, std::memory_order_release);
          return;
        }
      write_data_grow(&val, 1);
    }
  else if (detail::sDlWritePos < detail::sDlSize) {
    detail::sDlBuffer[detail::sDlWritePos++] = val;
  }
}

inline void write_u16(const uint16_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u32(const uint32_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_u64(const uint64_t val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

inline void write_f32(const float val) {
  const auto out = bswap(val);
  write_data(&out, sizeof(out));
}

// Overwrites an unpublished u32 previously written at the given offset.
void patch_u32(uint32_t offset, uint32_t val);

// Marks a complete draw and publishes when the configured draw batch is full.
void finish_draw() noexcept;

// Makes commands written so far available to the FIFO processor.
void publish() noexcept;

using DrawDoneCallback = void (*)();
DrawDoneCallback set_draw_done_callback(DrawDoneCallback callback) noexcept;
using DrawSyncCallback = void (*)(uint16_t);
DrawSyncCallback set_draw_sync_callback(DrawSyncCallback callback) noexcept;
uint16_t read_draw_sync() noexcept;
// True only inside a draw-sync callback; reads its point-in-stream EFB.
bool peek_draw_sync_z(uint16_t x, uint16_t y, uint32_t& z);

using BreakPointCallback = void (*)();
BreakPointCallback set_breakpoint_callback(BreakPointCallback callback) noexcept;
// GP ring offsets are translated while excluding command decoding. A passed
// address therefore refers to its next occurrence, just as on a hardware ring.
// Breakpoints must fall between complete encoded commands; partial-command
// fetch buffering is not implemented by this decoder.
void enable_breakpoint(uint64_t generation, uint64_t readOrigin, uint32_t initialReadOffset,
                       uint32_t ringSize, uint32_t breakOffset) noexcept;
void disable_breakpoint() noexcept;
// Interrupt-safe request: abandon pending commands without waiting for guest,
// decoder or GPU execution. drain() acknowledges retirement before returning.
void abort_frame() noexcept;

// Display list recording
void begin_display_list(uint8_t* buf, uint32_t size);
uint32_t end_display_list();
bool in_display_list();

// Ensure all buffered commands have been processed.
void drain();

// Internal buffer inspection
const uint8_t* get_buffer_data();
uint32_t get_buffer_size();
void clear_buffer();

// Logical ring addresses remain valid across decoder buffer growth and drains.
// The counters can also be read by the original GX control/interrupt threads.
// Consumed bytes are decoded commands, not GPU completion; completed includes
// return from their synchronous callbacks and is what drain() waits for.
struct CursorSnapshot {
  uint8_t* addressBase;
  uint32_t addressSize;
  uint64_t written;
  uint64_t published;
  uint64_t consumed;
  uint64_t completed;
  uint64_t generation;
  bool active;
  bool breakpoint;
};
CursorSnapshot cursor_snapshot();

} // namespace aurora::gx::fifo
