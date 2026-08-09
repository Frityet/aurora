#pragma once

#include "../internal.hpp"

namespace aurora::gx::fifo {

namespace detail {
bool checked_array_span_end(uint64_t offset, uint64_t length, uint32_t* end) noexcept;
}

// Process a buffer of GX FIFO commands
void process(const uint8_t* data, uint32_t size, bool bigEndian);

} // namespace aurora::gx::fifo
