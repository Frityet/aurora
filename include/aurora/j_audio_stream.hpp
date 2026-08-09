#pragma once

#include <aurora/audio.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace aurora::audio {

struct JAudioStreamRecipe {
  PcmVoiceSpec voice;
  std::uint32_t sample_rate = 0;
  std::uint32_t sample_count = 0;
  std::uint32_t loop_start = 0;
  std::uint32_t loop_end = 0;
  std::uint16_t channel_count = 0;
  bool looping = false;
};

// Decodes the retail JASAramStream STRM/BLCK PCM16-BE container. Channel
// pan comes from the BST stream item's packed two-bit controls.
[[nodiscard]] JAudioStreamRecipe decode_jaudio_stream(std::span<const std::uint8_t> bytes,
                                                      std::uint16_t channel_control);

} // namespace aurora::audio
