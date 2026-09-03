#include <aurora/j_audio_stream.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::audio {
namespace {
constexpr std::uint32_t fourcc(char a, char b, char c, char d) {
  return static_cast<std::uint32_t>(static_cast<unsigned char>(a)) << 24U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(b)) << 16U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(c)) << 8U |
         static_cast<std::uint32_t>(static_cast<unsigned char>(d));
}

[[noreturn]] void malformed(std::string_view detail) {
  throw std::runtime_error("Malformed JAudio stream resource: " + std::string(detail));
}

class Reader final {
public:
  explicit Reader(std::span<const std::uint8_t> bytes) : _bytes(bytes) {}

  void require(std::size_t offset, std::size_t length, std::string_view detail) const {
    if (offset > _bytes.size() || length > _bytes.size() - offset) {
      malformed(detail);
    }
  }

  [[nodiscard]] std::uint8_t u8(std::size_t offset, std::string_view detail) const {
    require(offset, 1U, detail);
    return _bytes[offset];
  }

  [[nodiscard]] std::uint16_t u16(std::size_t offset, std::string_view detail) const {
    require(offset, 2U, detail);
    return static_cast<std::uint16_t>(_bytes[offset]) << 8U | static_cast<std::uint16_t>(_bytes[offset + 1U]);
  }

  [[nodiscard]] std::int16_t s16(std::size_t offset, std::string_view detail) const {
    return std::bit_cast<std::int16_t>(u16(offset, detail));
  }

  [[nodiscard]] std::uint32_t u32(std::size_t offset, std::string_view detail) const {
    require(offset, 4U, detail);
    return static_cast<std::uint32_t>(_bytes[offset]) << 24U | static_cast<std::uint32_t>(_bytes[offset + 1U]) << 16U |
           static_cast<std::uint32_t>(_bytes[offset + 2U]) << 8U | static_cast<std::uint32_t>(_bytes[offset + 3U]);
  }

  [[nodiscard]] std::size_t size() const { return _bytes.size(); }

private:
  std::span<const std::uint8_t> _bytes;
};

[[nodiscard]] std::size_t align_32(std::size_t value) {
  if (value > std::numeric_limits<std::size_t>::max() - 31U) {
    malformed("block alignment overflows the host size type");
  }
  return (value + 31U) & ~std::size_t{31U};
}

[[nodiscard]] float channel_pan(std::uint16_t controls, std::size_t channel) {
  const auto control = static_cast<std::uint16_t>(controls >> (channel * 2U) & 3U);
  switch (control) {
  case 0U:
  case 1U:
    return 0.5F;
  case 2U:
    return 0.0F;
  case 3U:
    return 1.0F;
  default:
    malformed("invalid packed stream channel control");
  }
}
} // namespace

JAudioStreamRecipe decode_jaudio_stream(std::span<const std::uint8_t> bytes, std::uint16_t channel_control) {
  const auto reader = Reader{bytes};
  reader.require(0U, 0x40U, "truncated STRM header");
  if (reader.u32(0U, "truncated STRM header") != fourcc('S', 'T', 'R', 'M')) {
    malformed("invalid STRM signature");
  }
  if (reader.u32(4U, "truncated STRM size") != reader.size() - 0x40U) {
    malformed("STRM payload size disagrees with the file size");
  }
  if (reader.u8(9U, "truncated STRM format") != 1U || reader.u16(0x0aU, "truncated STRM bit depth") != 16U) {
    malformed("only retail PCM16-BE STRM data is supported");
  }

  const auto channels = reader.u16(0x0cU, "truncated STRM channel count");
  const auto loop_flag = reader.u16(0x0eU, "truncated STRM loop flag");
  const auto sample_rate = reader.u32(0x10U, "truncated STRM sample rate");
  const auto sample_count = reader.u32(0x14U, "truncated STRM sample count");
  const auto loop_start = reader.u32(0x18U, "truncated STRM loop start");
  const auto loop_end = reader.u32(0x1cU, "truncated STRM loop end");
  const auto maximum_block_bytes = reader.u32(0x20U, "truncated STRM block size");
  const auto header_gain = reader.u8(0x28U, "truncated STRM gain");
  if (channels == 0U || channels > 8U || sample_rate == 0U || sample_count == 0U || maximum_block_bytes == 0U ||
      (maximum_block_bytes & 1U) != 0U || header_gain > 127U) {
    malformed("invalid STRM scalar metadata");
  }
  const auto looping = loop_flag != 0U;
  if ((looping && (loop_start >= loop_end || loop_end > sample_count)) ||
      (!looping && (loop_start != 0U || loop_end > sample_count))) {
    malformed("invalid STRM loop metadata");
  }

  // Every decoded sample requires two payload bytes per channel, in addition
  // to at least one BLCK header. Prove that the declared count can fit in this
  // file before reserving host vectors; otherwise a tiny corrupt header could
  // request gigabytes of memory before the block parser sees the truncation.
  constexpr auto block_header_size = std::size_t{0x20U};
  const auto payload_size = reader.size() - 0x40U;
  if (payload_size < block_header_size) {
    malformed("STRM payload cannot contain a BLCK header");
  }
  const auto sample_payload_capacity = payload_size - block_header_size;
  const auto minimum_sample_payload =
      static_cast<std::uint64_t>(sample_count) * static_cast<std::uint64_t>(channels) * 2U;
  if (minimum_sample_payload > sample_payload_capacity) {
    malformed("declared STRM sample count cannot fit in the block payload");
  }

  auto channel_samples = std::vector<std::vector<float>>(channels);
  for (auto& samples : channel_samples) {
    if (sample_count > samples.max_size()) {
      malformed("declared STRM sample count exceeds the host vector limit");
    }
    samples.reserve(sample_count);
  }

  auto cursor = std::size_t{0x40U};
  while (cursor < reader.size()) {
    reader.require(cursor, 0x20U, "truncated BLCK header");
    if (reader.u32(cursor, "truncated BLCK header") != fourcc('B', 'L', 'C', 'K')) {
      malformed("invalid BLCK signature");
    }
    const auto channel_bytes = reader.u32(cursor + 4U, "truncated BLCK channel size");
    if (channel_bytes == 0U || channel_bytes > maximum_block_bytes || (channel_bytes & 1U) != 0U) {
      malformed("invalid BLCK channel size");
    }
    cursor += 0x20U;
    for (auto channel = std::size_t{0}; channel < channels; ++channel) {
      reader.require(cursor, channel_bytes, "BLCK channel payload extends outside the stream");
      auto& samples = channel_samples[channel];
      for (auto offset = std::size_t{0}; offset < channel_bytes; offset += 2U) {
        if (samples.size() < sample_count) {
          samples.push_back(static_cast<float>(reader.s16(cursor + offset, "truncated BLCK PCM sample")) / 32768.0F);
        }
      }
      cursor += channel_bytes;
    }
    if (cursor == reader.size()) {
      break;
    }
    const auto aligned = align_32(cursor);
    reader.require(cursor, aligned - cursor, "BLCK padding extends outside the stream");
    for (auto offset = cursor; offset < aligned; ++offset) {
      if (reader.u8(offset, "truncated BLCK padding") != 0U) {
        malformed("BLCK alignment padding is not zero");
      }
    }
    cursor = aligned;
  }
  if (cursor != reader.size() || std::ranges::any_of(channel_samples, [sample_count](const auto& samples) {
        return samples.size() != sample_count;
      })) {
    malformed("BLCK payload does not contain the declared sample count");
  }

  auto result = JAudioStreamRecipe{
      .sample_rate = sample_rate,
      .sample_count = sample_count,
      .loop_start = loop_start,
      .loop_end = loop_end,
      .channel_count = channels,
      .looping = looping,
  };
  result.voice.layers.reserve(channels);
  const auto gain = static_cast<float>(header_gain) / 127.0F;
  for (auto channel = std::size_t{0}; channel < channels; ++channel) {
    result.voice.layers.push_back(PcmLayer{
        .samples = std::make_shared<const std::vector<float>>(std::move(channel_samples[channel])),
        .sample_rate = sample_rate,
        .loop_start = looping ? loop_start : 0U,
        .loop_end = looping ? loop_end : 0U,
        .gain = gain,
        .pan = channel_pan(channel_control, channel),
    });
  }
  return result;
}

} // namespace aurora::audio
