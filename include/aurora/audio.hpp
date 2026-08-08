#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace aurora::audio {

struct AfcState {
  std::int16_t previous_sample = 0;
  std::int16_t older_sample = 0;
};

struct AfcDecodeResult {
  std::vector<std::int16_t> samples;
  AfcState final_state;
};

using AfcCoefficientTable = std::array<std::int16_t, 32>;

// The global AFC table uploaded by JAudio's DsetupTable call. WSYS wave
// records do not carry per-wave coefficient tables.
[[nodiscard]] const AfcCoefficientTable& jaudio_afc_coefficients();

// Decodes Nintendo/JAudio high-quality AFC: 16 samples in each 9-byte block.
// The header's high nibble is the delta exponent and its low nibble selects
// one of the sixteen coefficient pairs. This is distinct from the common
// 14-sample/8-byte standalone DSP-ADPCM container format.
[[nodiscard]] AfcDecodeResult decode_afc_hq(std::span<const std::uint8_t> encoded, std::size_t sample_count,
                                            const AfcCoefficientTable& coefficients, AfcState initial_state = {});

[[nodiscard]] AfcDecodeResult decode_jaudio_afc_hq(std::span<const std::uint8_t> encoded, std::size_t sample_count,
                                                   AfcState initial_state = {});

enum class EnvelopeCurve {
  Linear,
  // JAudio's curve 3 (JASOscillator::sCurveSampleCell), used by volume
  // oscillator release envelopes in retail JAudio banks.
  JAudioSampleCell,
};

struct LoopingPcmLayer {
  std::shared_ptr<const std::vector<float>> samples;
  std::uint32_t sample_rate = 0;
  std::size_t loop_start = 0;
  std::size_t loop_end = 0;
  float gain = 1.0F;
  float pitch_ratio = 1.0F;
  float pan = 0.5F;
  double start_delay_seconds = 0.0;
  double attack_seconds = 0.0;
  double release_seconds = 0.0;
  EnvelopeCurve attack_curve = EnvelopeCurve::Linear;
  EnvelopeCurve release_curve = EnvelopeCurve::Linear;
};

struct LoopingVoiceSpec {
  std::vector<LoopingPcmLayer> layers;
  float gain_multiplier = 1.0F;
  float pitch_multiplier = 1.0F;
};

struct VoiceToken {
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const { return value != 0; }
  [[nodiscard]] friend bool operator==(VoiceToken, VoiceToken) = default;
};

struct PlaybackStats {
  std::uint64_t mixed_frames = 0;
  std::uint64_t nonzero_samples = 0;
  std::uint64_t device_callbacks = 0;
};

enum class PlaybackDevicePolicy {
  RequireAudibleOutput,
  AllowExplicitTestSink,
};

// A small, deterministic looping-voice mixer. The same rendering path drives
// both focused tests and the SDL playback callback.
class LoopingAudioMixer final {
public:
  explicit LoopingAudioMixer(std::uint32_t output_sample_rate = 48000,
                             PlaybackDevicePolicy device_policy = PlaybackDevicePolicy::RequireAudibleOutput);
  ~LoopingAudioMixer();

  LoopingAudioMixer(const LoopingAudioMixer&) = delete;
  LoopingAudioMixer& operator=(const LoopingAudioMixer&) = delete;
  LoopingAudioMixer(LoopingAudioMixer&&) = delete;
  LoopingAudioMixer& operator=(LoopingAudioMixer&&) = delete;

  // Opens and resumes SDL's default playback device. Throws with SDL's exact
  // error if no real playback stream can be established.
  void open_default_playback();
  void close_default_playback();
  [[nodiscard]] bool is_device_open() const;

  [[nodiscard]] VoiceToken start_voice(const LoopingVoiceSpec& spec);
  // Atomically updates both live parameters. Returns false when the callback
  // has already retired the token, allowing the owner to attach a new voice
  // without a check/update race.
  [[nodiscard]] bool try_update_voice(VoiceToken token, float gain_multiplier, float pitch_multiplier);
  void set_voice_gain(VoiceToken token, float gain_multiplier);
  void set_voice_pitch(VoiceToken token, float pitch_multiplier);
  void release_voice(VoiceToken token);
  void stop_voice(VoiceToken token);
  void stop_all_voices();
  [[nodiscard]] bool is_voice_active(VoiceToken token) const;
  [[nodiscard]] std::optional<float> voice_gain_multiplier(VoiceToken token) const;
  [[nodiscard]] std::optional<float> voice_pitch_multiplier(VoiceToken token) const;

  // Writes interleaved stereo float samples. The span length must be even.
  void render_interleaved(std::span<float> output);

  [[nodiscard]] std::uint32_t output_sample_rate() const;
  [[nodiscard]] PlaybackStats stats() const;

private:
  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

} // namespace aurora::audio
