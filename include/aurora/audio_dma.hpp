#pragma once

#include <cstdint>
#include <memory>
#include <span>

namespace aurora::audio {
// Device boundary for already mixed, signed PCM DMA buffers. No voices,
// instruments, envelopes or game sound IDs are interpreted here.
class DmaAudioOutput {
public:
  explicit DmaAudioOutput(int sample_rate);
  ~DmaAudioOutput();
  void submit(std::span<const std::int16_t> stereo);
  void set_gain(float gain);
  std::uint64_t queued_frames() const;
  std::uint64_t submitted_frames() const;
  std::uint64_t nonzero_samples() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};
} // namespace aurora::audio
