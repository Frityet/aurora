#pragma once

#include "../internal.hpp"

#include <cstdint>

namespace aurora::gx::fifo {

namespace detail {
bool checked_array_span_end(uint64_t offset, uint64_t length, uint32_t* end) noexcept;
}

struct ProcessResult {
  uint32_t bytesProcessed;
  bool drawDone;
};

// Process GX FIFO commands until the next draw done event or end of buffer
ProcessResult process(const uint8_t* data, uint32_t size) noexcept;
void clear_draw_cache() noexcept;

} // namespace aurora::gx::fifo
