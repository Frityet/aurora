#pragma once

#include "../internal.hpp"
#include "../gfx/command_epoch.hpp"

#include <cstdint>

namespace aurora::gx::fifo {

namespace detail {
bool checked_array_span_end(uint64_t offset, uint64_t length, uint32_t* end) noexcept;
}

struct ProcessResult {
  uint32_t bytesProcessed;
  bool drawDone;
  bool tokenWrite = false;
  bool tokenInterrupt = false;
  uint16_t token = 0;
  bool incomplete = false;
  bool displayListCall = false;
  const uint8_t* displayListData = nullptr;
  uint32_t displayListSize = 0;
};

enum class InputMode {
  Complete,
  Streaming,
  // Finite list input; hardware does not execute calls nested inside a list.
  DisplayList,
};

// Streaming input can end inside a command. Its prefix remains unprocessed
// until more bytes arrive; complete display lists still reject truncated data.
ProcessResult process(const uint8_t* data, uint32_t size, gfx::CommandEpoch epoch = {},
                      InputMode mode = InputMode::Complete) noexcept;
void clear_draw_cache() noexcept;

} // namespace aurora::gx::fifo
