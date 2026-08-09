#include <aurora/audio.hpp>

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <mutex>
#include <numbers>
#include <stdexcept>
#include <string>
#include <utility>

namespace aurora::audio {
namespace {

constexpr auto kJAudioAfcCoefficients = AfcCoefficientTable{
    0x0000,  0x0000,  0x0800,  0x0000,  0x0000,  0x0800, 0x0400,  0x0400, 0x1000,  -0x0800, 0x0e00,
    -0x0600, 0x0c00,  -0x0400, 0x1200,  -0x0a00, 0x1068, -0x08c8, 0x12c0, -0x08fc, 0x1400,  -0x0c00,
    0x0800,  -0x0800, 0x0400,  -0x0400, -0x0400, 0x0400, -0x0400, 0x0000, -0x0800, 0x0000,
};

constexpr auto kJAudioSampleCell = std::array<float, 17>{
    1.0F,          0.9704890251F, 0.7812740207F, 0.5462809801F, 0.3997919858F, 0.2893149853F,
    0.2121039927F, 0.1574759930F, 0.112613F,     0.0817895979F, 0.0579852015F, 0.0436415002F,
    0.0308237001F, 0.0237128995F, 0.0152593004F, 0.00915555F,   0.0F,
};

[[nodiscard]] float envelope_value(EnvelopeCurve curve, double progress) {
  const auto clamped = std::clamp(progress, 0.0, 1.0);
  if (curve == EnvelopeCurve::Linear) {
    return static_cast<float>(1.0 - clamped);
  }

  const auto table_position = clamped * 16.0;
  const auto first = static_cast<std::size_t>(table_position);
  if (first >= 16) {
    return 0.0F;
  }
  const auto fraction = static_cast<float>(table_position - static_cast<double>(first));
  return std::lerp(kJAudioSampleCell[first], kJAudioSampleCell[first + 1], fraction);
}

[[nodiscard]] std::int32_t sign_extend_nibble(std::uint8_t nibble) {
  return (nibble & 0x08U) != 0U ? static_cast<std::int32_t>(nibble) - 16 : static_cast<std::int32_t>(nibble);
}

} // namespace

AfcDecodeResult decode_afc_hq(std::span<const std::uint8_t> encoded, std::size_t sample_count,
                              const AfcCoefficientTable& coefficients, AfcState initial_state) {
  const auto required_frames = (sample_count + 15U) / 16U;
  if (required_frames > encoded.size() / 9U) {
    throw std::invalid_argument("AFC HQ payload is shorter than its declared sample count");
  }

  auto result = AfcDecodeResult{};
  result.samples.reserve(sample_count);
  auto previous = static_cast<std::int32_t>(initial_state.previous_sample);
  auto older = static_cast<std::int32_t>(initial_state.older_sample);

  for (auto frame_index = std::size_t{0}; frame_index < required_frames; ++frame_index) {
    const auto frame = encoded.subspan(frame_index * 9U, 9U);
    const auto exponent = frame[0] >> 4U;
    // Dolphin's Zelda/JAudio renderer stores 1 << exponent in s16. The
    // exponent-15 value therefore wraps to -32768 rather than +32768.
    const auto delta = exponent == 15U ? std::int32_t{-0x8000} : static_cast<std::int32_t>(1U << exponent);
    const auto coefficient_index = static_cast<std::size_t>(frame[0] & 0x0fU);
    const auto coefficient_1 = static_cast<std::int32_t>(coefficients[coefficient_index * 2U]);
    const auto coefficient_2 = static_cast<std::int32_t>(coefficients[coefficient_index * 2U + 1U]);

    for (auto sample_in_frame = std::size_t{0}; sample_in_frame < 16U; ++sample_in_frame) {
      if (result.samples.size() == sample_count) {
        break;
      }
      const auto packed = frame[1U + sample_in_frame / 2U];
      const auto nibble = (sample_in_frame & 1U) == 0U ? static_cast<std::uint8_t>(packed >> 4U)
                                                       : static_cast<std::uint8_t>(packed & 0x0fU);
      // Dolphin's Zelda/JAudio HLE expands each signed residual to 4.11
      // before the combined predictor sum and one arithmetic >> 11.
      const auto residual = sign_extend_nibble(nibble) * 0x0800;
      auto decoded = (delta * residual + coefficient_1 * previous + coefficient_2 * older) >> 11U;
      decoded = std::clamp(decoded, -0x8000, 0x7fff);
      older = previous;
      previous = decoded;
      result.samples.push_back(static_cast<std::int16_t>(decoded));
    }
  }

  result.final_state.previous_sample = static_cast<std::int16_t>(previous);
  result.final_state.older_sample = static_cast<std::int16_t>(older);
  return result;
}

const AfcCoefficientTable& jaudio_afc_coefficients() { return kJAudioAfcCoefficients; }

AfcDecodeResult decode_jaudio_afc_hq(std::span<const std::uint8_t> encoded, std::size_t sample_count,
                                     AfcState initial_state) {
  return decode_afc_hq(encoded, sample_count, kJAudioAfcCoefficients, initial_state);
}

struct PcmAudioMixer::Impl {
  struct GainRamp {
    float start = 1.0F;
    float target = 1.0F;
    std::uint64_t total_frames = 0;
    std::uint64_t elapsed_frames = 0;

    [[nodiscard]] bool active() const { return elapsed_frames < total_frames; }

    [[nodiscard]] float value() const {
      if (!active()) {
        return target;
      }
      return std::lerp(start, target, static_cast<float>(elapsed_frames) / static_cast<float>(total_frames));
    }

    void advance() {
      if (active()) {
        ++elapsed_frames;
      }
    }
  };

  struct LayerState {
    PcmLayer spec;
    double source_position = 0.0;
    std::uint64_t elapsed_output_frames = 0;
    double release_elapsed_seconds = 0.0;
    float release_start_envelope = 0.0F;
    GainRamp gain_ramp;
    bool release_started = false;
    bool finished = false;
  };

  struct VoiceState {
    VoiceToken token;
    std::vector<LayerState> layers;
    std::uint64_t rendered_frames = 0;
    float pitch_multiplier = 1.0F;
    GainRamp gain_ramp;
    bool stop_after_gain_ramp = false;
    bool paused = false;
    bool releasing = false;
  };

  explicit Impl(std::uint32_t rate, PlaybackDevicePolicy policy) : output_rate(rate), device_policy(policy) {}

  [[nodiscard]] std::uint64_t fade_frame_count(double duration_seconds) const {
    if (!std::isfinite(duration_seconds) || duration_seconds < 0.0) {
      throw std::invalid_argument("A PCM voice fade duration must be finite and nonnegative");
    }
    if (duration_seconds == 0.0) {
      return 0U;
    }
    return std::max<std::uint64_t>(
        1U, static_cast<std::uint64_t>(std::llround(duration_seconds * static_cast<double>(output_rate))));
  }

  void configure_gain_ramp(GainRamp& ramp, float current, float target, double duration_seconds) const {
    const auto frames = fade_frame_count(duration_seconds);
    ramp = GainRamp{
        .start = current,
        .target = target,
        .total_frames = frames,
        .elapsed_frames = 0U,
    };
  }

  [[nodiscard]] float attack_envelope(const LayerState& layer) const {
    const auto delay_frames =
        static_cast<std::uint64_t>(std::llround(layer.spec.start_delay_seconds * static_cast<double>(output_rate)));
    if (layer.elapsed_output_frames < delay_frames) {
      return 0.0F;
    }
    if (layer.spec.attack_seconds <= 0.0) {
      return 1.0F;
    }
    const auto attack_frames = layer.spec.attack_seconds * static_cast<double>(output_rate);
    const auto age = static_cast<double>(layer.elapsed_output_frames - delay_frames);
    const auto remaining = envelope_value(layer.spec.attack_curve, age / attack_frames);
    return 1.0F - remaining;
  }

  void begin_layer_release(LayerState& layer) const {
    if (layer.release_started || layer.finished) {
      return;
    }
    layer.release_start_envelope = attack_envelope(layer);
    layer.release_elapsed_seconds = 0.0;
    layer.release_started = true;
    if (layer.release_start_envelope <= 0.0F || layer.spec.release_seconds <= 0.0) {
      layer.finished = true;
    }
  }

  void begin_release(VoiceState& voice) {
    if (voice.releasing) {
      return;
    }
    voice.releasing = true;
    for (auto& layer : voice.layers) {
      begin_layer_release(layer);
    }
  }

  void reclaim_finished_voices() {
    std::erase_if(voices, [](const VoiceState& voice) {
      return std::ranges::all_of(voice.layers, [](const LayerState& layer) { return layer.finished; });
    });
  }

  [[nodiscard]] bool validate_layer(const PcmLayer& layer) const {
    const auto finite = layer.loop_start == 0U && layer.loop_end == 0U;
    const auto valid_loop =
        layer.samples != nullptr && layer.loop_start < layer.loop_end && layer.loop_end <= layer.samples->size();
    return layer.samples != nullptr && !layer.samples->empty() && layer.sample_rate != 0U && (finite || valid_loop) &&
           std::isfinite(layer.gain) && layer.gain >= 0.0F && std::isfinite(layer.pitch_ratio) &&
           layer.pitch_ratio > 0.0F && std::isfinite(layer.pitch_sweep_semitones) &&
           std::isfinite(layer.pitch_sweep_seconds) && layer.pitch_sweep_seconds >= 0.0 && std::isfinite(layer.pan) &&
           layer.pan >= 0.0F && layer.pan <= 1.0F && std::isfinite(layer.start_delay_seconds) &&
           layer.start_delay_seconds >= 0.0 && std::isfinite(layer.gate_seconds) && layer.gate_seconds >= 0.0 &&
           std::isfinite(layer.attack_seconds) && layer.attack_seconds >= 0.0 && std::isfinite(layer.release_seconds) &&
           layer.release_seconds >= 0.0;
  }

  [[nodiscard]] float sample_layer(LayerState& layer, float voice_pitch) const {
    const auto delay_frames =
        static_cast<std::uint64_t>(std::llround(layer.spec.start_delay_seconds * static_cast<double>(output_rate)));
    if (layer.elapsed_output_frames < delay_frames) {
      ++layer.elapsed_output_frames;
      layer.gain_ramp.advance();
      return 0.0F;
    }

    if (!layer.release_started && layer.spec.gate_seconds > 0.0) {
      const auto gate_frames =
          static_cast<std::uint64_t>(std::llround(layer.spec.gate_seconds * static_cast<double>(output_rate)));
      if (layer.elapsed_output_frames - delay_frames >= gate_frames) {
        begin_layer_release(layer);
        if (layer.finished) {
          return 0.0F;
        }
      }
    }

    auto envelope = attack_envelope(layer);
    if (layer.release_started) {
      const auto progress = layer.release_elapsed_seconds / layer.spec.release_seconds;
      envelope = layer.release_start_envelope * envelope_value(layer.spec.release_curve, progress);
      if (progress >= 1.0) {
        layer.finished = true;
        return 0.0F;
      }
    }

    const auto& samples = *layer.spec.samples;
    const auto looping = layer.spec.loop_end != 0U;
    if (!looping && layer.source_position >= static_cast<double>(samples.size())) {
      layer.finished = true;
      return 0.0F;
    }
    const auto position_floor = static_cast<std::size_t>(layer.source_position);
    const auto next_position =
        looping ? (position_floor + 1U < layer.spec.loop_end ? position_floor + 1U : layer.spec.loop_start)
                : std::min(position_floor + 1U, samples.size() - 1U);
    const auto fraction = static_cast<float>(layer.source_position - static_cast<double>(position_floor));
    const auto sample = std::lerp(samples[position_floor], samples[next_position], fraction) * layer.spec.gain *
                        layer.gain_ramp.value() * envelope;

    auto layer_pitch = static_cast<double>(layer.spec.pitch_ratio);
    if (layer.spec.pitch_sweep_seconds > 0.0) {
      const auto age_frames = layer.elapsed_output_frames - delay_frames;
      const auto progress = std::clamp(static_cast<double>(age_frames) /
                                           (layer.spec.pitch_sweep_seconds * static_cast<double>(output_rate)),
                                       0.0, 1.0);
      layer_pitch *= std::exp2(static_cast<double>(layer.spec.pitch_sweep_semitones) * progress / 12.0);
    }
    const auto step = static_cast<double>(layer.spec.sample_rate) / static_cast<double>(output_rate) * layer_pitch *
                      static_cast<double>(voice_pitch);
    layer.source_position += step;
    if (looping) {
      while (layer.source_position >= static_cast<double>(layer.spec.loop_end)) {
        layer.source_position = static_cast<double>(layer.spec.loop_start) +
                                (layer.source_position - static_cast<double>(layer.spec.loop_end));
      }
    } else if (layer.source_position >= static_cast<double>(samples.size())) {
      layer.finished = true;
    }
    ++layer.elapsed_output_frames;
    layer.gain_ramp.advance();
    if (layer.release_started) {
      layer.release_elapsed_seconds += 1.0 / static_cast<double>(output_rate);
    }
    return sample;
  }

  void render_locked(std::span<float> output, bool device_callback) {
    std::ranges::fill(output, 0.0F);
    const auto frame_count = output.size() / 2U;
    for (auto frame = std::size_t{0}; frame < frame_count; ++frame) {
      auto left = 0.0F;
      auto right = 0.0F;
      for (auto& voice : voices) {
        if (voice.paused) {
          continue;
        }
        const auto voice_gain = voice.gain_ramp.value();
        for (auto& layer : voice.layers) {
          if (layer.finished) {
            continue;
          }
          const auto sample = sample_layer(layer, voice.pitch_multiplier) * voice_gain;
          // Constant-power pan. JAudio's neutral pan is 0.5.
          const auto pan_angle = std::numbers::pi_v<float> * 0.5F * layer.spec.pan;
          left += sample * std::cos(pan_angle);
          right += sample * std::sin(pan_angle);
        }
        voice.gain_ramp.advance();
        if (voice.stop_after_gain_ramp && !voice.gain_ramp.active()) {
          for (auto& layer : voice.layers) {
            layer.finished = true;
          }
        }
        ++voice.rendered_frames;
      }
      output[frame * 2U] = std::clamp(left, -1.0F, 1.0F);
      output[frame * 2U + 1U] = std::clamp(right, -1.0F, 1.0F);
    }

    stats.mixed_frames += frame_count;
    stats.nonzero_samples += static_cast<std::uint64_t>(
        std::ranges::count_if(output, [](float value) { return std::abs(value) > 1.0e-8F; }));
    if (device_callback) {
      ++stats.device_callbacks;
    }
  }

  static void SDLCALL audio_callback(void* userdata, SDL_AudioStream* stream, int additional_amount, int) noexcept {
    auto& self = *static_cast<Impl*>(userdata);
    if (additional_amount <= 0) {
      return;
    }

    // Avoid allocations and exception recovery in the real-time callback.
    // A failed SDL submission marks the device unusable; it is never treated
    // as successful silent playback.
    auto buffer = std::array<float, 2048>{};
    auto remaining_samples = static_cast<std::size_t>(additional_amount) / sizeof(float);
    remaining_samples -= remaining_samples & 1U;
    const auto lock = std::scoped_lock(self.mutex);
    ++self.stats.device_callbacks;
    while (remaining_samples != 0U && !self.device_failed.load(std::memory_order_relaxed)) {
      const auto count = std::min(remaining_samples, buffer.size());
      self.render_locked(std::span<float>(buffer).first(count), false);
      if (!SDL_PutAudioStreamData(stream, buffer.data(), static_cast<int>(count * sizeof(float)))) {
        self.device_failed.store(true, std::memory_order_relaxed);
        break;
      }
      remaining_samples -= count;
    }
  }

  static bool SDLCALL event_watch(void* userdata, SDL_Event* event) noexcept {
    auto& self = *static_cast<Impl*>(userdata);
    if (event->type == SDL_EVENT_AUDIO_DEVICE_REMOVED && !event->adevice.recording) {
      // SDL keeps default-open streams bound to a silent zombie after their
      // physical output disappears. Conservatively fail this mixer on any
      // playback-device removal; a later call will surface the loss instead
      // of reporting successful inaudible playback.
      self.device_failed.store(true, std::memory_order_relaxed);
    }
    return true;
  }

  mutable std::mutex mutex;
  std::uint32_t output_rate = 48000;
  std::uint64_t next_token = 1;
  std::vector<VoiceState> voices;
  PlaybackStats stats;
  SDL_AudioStream* stream = nullptr;
  bool owns_sdl_audio_ref = false;
  bool owns_sdl_events_ref = false;
  bool event_watch_registered = false;
  std::atomic_bool device_failed = false;
  PlaybackDevicePolicy device_policy = PlaybackDevicePolicy::RequireAudibleOutput;
};

PcmAudioMixer::PcmAudioMixer(std::uint32_t output_sample_rate, PlaybackDevicePolicy device_policy)
: m_impl(std::make_unique<Impl>(output_sample_rate, device_policy)) {
  if (output_sample_rate == 0U) {
    throw std::invalid_argument("Audio mixer output sample rate must be nonzero");
  }
}

PcmAudioMixer::~PcmAudioMixer() { close_default_playback(); }

void PcmAudioMixer::open_default_playback() {
  const auto lock = std::scoped_lock(m_impl->mutex);
  if (m_impl->stream != nullptr) {
    if (m_impl->device_failed.load(std::memory_order_relaxed)) {
      throw std::runtime_error("SDL playback stream stopped accepting mixed audio");
    }
    return;
  }
  if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
    throw std::runtime_error(std::string("SDL audio initialization failed: ") + SDL_GetError());
  }
  m_impl->owns_sdl_audio_ref = true;
  if (!SDL_InitSubSystem(SDL_INIT_EVENTS)) {
    const auto error = std::string(SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_impl->owns_sdl_audio_ref = false;
    throw std::runtime_error(std::string("SDL event initialization failed: ") + error);
  }
  m_impl->owns_sdl_events_ref = true;
  m_impl->device_failed.store(false, std::memory_order_relaxed);
  if (!SDL_AddEventWatch(Impl::event_watch, m_impl.get())) {
    const auto error = std::string(SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_impl->owns_sdl_events_ref = false;
    m_impl->owns_sdl_audio_ref = false;
    throw std::runtime_error("SDL audio device watch failed: " + error);
  }
  m_impl->event_watch_registered = true;
  const auto* driver = SDL_GetCurrentAudioDriver();
  if (m_impl->device_policy == PlaybackDevicePolicy::RequireAudibleOutput && driver != nullptr &&
      (std::string_view(driver) == "dummy" || std::string_view(driver) == "disk")) {
    const auto rejected_driver = std::string(driver);
    SDL_RemoveEventWatch(Impl::event_watch, m_impl.get());
    m_impl->event_watch_registered = false;
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_impl->owns_sdl_events_ref = false;
    m_impl->owns_sdl_audio_ref = false;
    throw std::runtime_error("SDL audio driver is not an audible playback device: " + rejected_driver);
  }

  const auto spec = SDL_AudioSpec{
      .format = SDL_AUDIO_F32,
      .channels = 2,
      .freq = static_cast<int>(m_impl->output_rate),
  };
  m_impl->stream =
      SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, Impl::audio_callback, m_impl.get());
  if (m_impl->stream == nullptr) {
    const auto error = std::string(SDL_GetError());
    SDL_RemoveEventWatch(Impl::event_watch, m_impl.get());
    m_impl->event_watch_registered = false;
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_impl->owns_sdl_events_ref = false;
    m_impl->owns_sdl_audio_ref = false;
    throw std::runtime_error("SDL default playback stream failed: " + error);
  }
  if (!SDL_ResumeAudioStreamDevice(m_impl->stream)) {
    const auto error = std::string(SDL_GetError());
    SDL_DestroyAudioStream(std::exchange(m_impl->stream, nullptr));
    SDL_RemoveEventWatch(Impl::event_watch, m_impl.get());
    m_impl->event_watch_registered = false;
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    m_impl->owns_sdl_events_ref = false;
    m_impl->owns_sdl_audio_ref = false;
    throw std::runtime_error("SDL default playback resume failed: " + error);
  }
}

void PcmAudioMixer::close_default_playback() {
  SDL_AudioStream* stream = nullptr;
  auto quit_audio = false;
  auto quit_events = false;
  auto remove_event_watch = false;
  {
    const auto lock = std::scoped_lock(m_impl->mutex);
    stream = std::exchange(m_impl->stream, nullptr);
    quit_audio = std::exchange(m_impl->owns_sdl_audio_ref, false);
    quit_events = std::exchange(m_impl->owns_sdl_events_ref, false);
    remove_event_watch = std::exchange(m_impl->event_watch_registered, false);
  }
  if (remove_event_watch) {
    SDL_RemoveEventWatch(Impl::event_watch, m_impl.get());
  }
  if (stream != nullptr) {
    SDL_DestroyAudioStream(stream);
  }
  if (quit_events) {
    SDL_QuitSubSystem(SDL_INIT_EVENTS);
  }
  if (quit_audio) {
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
  m_impl->device_failed.store(false, std::memory_order_relaxed);
}

bool PcmAudioMixer::is_device_open() const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  return m_impl->stream != nullptr && !m_impl->device_failed.load(std::memory_order_relaxed);
}

VoiceToken PcmAudioMixer::start_voice(const PcmVoiceSpec& spec) {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  if (spec.layers.empty()) {
    throw std::invalid_argument("A PCM voice must contain at least one layer");
  }
  if (!std::isfinite(spec.gain_multiplier) || spec.gain_multiplier < 0.0F) {
    throw std::invalid_argument("A PCM voice gain multiplier must be nonnegative");
  }
  if (!std::isfinite(spec.pitch_multiplier) || spec.pitch_multiplier <= 0.0F) {
    throw std::invalid_argument("A PCM voice pitch multiplier must be positive");
  }
  if (!std::ranges::all_of(spec.layers, [this](const auto& layer) { return m_impl->validate_layer(layer); })) {
    throw std::invalid_argument("A PCM voice contains an invalid layer");
  }

  auto token = VoiceToken{m_impl->next_token++};
  if (!token) {
    token = VoiceToken{m_impl->next_token++};
  }
  auto voice = Impl::VoiceState{
      .token = token,
      .layers = {},
      .pitch_multiplier = spec.pitch_multiplier,
      .gain_ramp =
          Impl::GainRamp{
              .start = spec.gain_multiplier,
              .target = spec.gain_multiplier,
          },
  };
  voice.layers.reserve(spec.layers.size());
  for (const auto& layer : spec.layers) {
    voice.layers.push_back(Impl::LayerState{.spec = layer});
  }
  m_impl->voices.push_back(std::move(voice));
  return token;
}

bool PcmAudioMixer::try_update_voice(VoiceToken token, float gain_multiplier, float pitch_multiplier) {
  if (!std::isfinite(gain_multiplier) || gain_multiplier < 0.0F) {
    throw std::invalid_argument("A PCM voice gain multiplier must be nonnegative");
  }
  if (!std::isfinite(pitch_multiplier) || pitch_multiplier <= 0.0F) {
    throw std::invalid_argument("A PCM voice pitch multiplier must be positive");
  }

  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return false;
  }
  voice->gain_ramp = Impl::GainRamp{.start = gain_multiplier, .target = gain_multiplier};
  voice->stop_after_gain_ramp = false;
  voice->pitch_multiplier = pitch_multiplier;
  return true;
}

void PcmAudioMixer::set_voice_gain(VoiceToken token, float gain_multiplier) {
  if (!std::isfinite(gain_multiplier) || gain_multiplier < 0.0F) {
    throw std::invalid_argument("A PCM voice gain multiplier must be nonnegative");
  }
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    throw std::logic_error("Cannot update an inactive PCM voice");
  }
  voice->gain_ramp = Impl::GainRamp{.start = gain_multiplier, .target = gain_multiplier};
  voice->stop_after_gain_ramp = false;
}

void PcmAudioMixer::set_voice_pitch(VoiceToken token, float pitch_multiplier) {
  if (!std::isfinite(pitch_multiplier) || pitch_multiplier <= 0.0F) {
    throw std::invalid_argument("A PCM voice pitch multiplier must be positive");
  }
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    throw std::logic_error("Cannot update an inactive PCM voice");
  }
  voice->pitch_multiplier = pitch_multiplier;
}

void PcmAudioMixer::fade_voice_gain(VoiceToken token, float gain_multiplier, double duration_seconds) {
  if (!std::isfinite(gain_multiplier) || gain_multiplier < 0.0F) {
    throw std::invalid_argument("A PCM voice gain multiplier must be nonnegative");
  }
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    throw std::logic_error("Cannot fade an inactive PCM voice");
  }
  m_impl->configure_gain_ramp(voice->gain_ramp, voice->gain_ramp.value(), gain_multiplier, duration_seconds);
  voice->stop_after_gain_ramp = false;
}

void PcmAudioMixer::fade_out_voice(VoiceToken token, double duration_seconds) {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return;
  }
  m_impl->configure_gain_ramp(voice->gain_ramp, voice->gain_ramp.value(), 0.0F, duration_seconds);
  voice->stop_after_gain_ramp = true;
  if (!voice->gain_ramp.active()) {
    for (auto& layer : voice->layers) {
      layer.finished = true;
    }
  }
}

void PcmAudioMixer::fade_layer_gains(VoiceToken token, std::span<const float> gain_multipliers,
                                     double duration_seconds) {
  if (!std::ranges::all_of(gain_multipliers, [](float gain) { return std::isfinite(gain) && gain >= 0.0F; })) {
    throw std::invalid_argument("PCM layer gain multipliers must be finite and nonnegative");
  }
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    throw std::logic_error("Cannot fade layers on an inactive PCM voice");
  }
  if (gain_multipliers.size() != voice->layers.size()) {
    throw std::invalid_argument("PCM layer gain count must match the voice layer count");
  }
  for (auto index = std::size_t{0}; index < voice->layers.size(); ++index) {
    auto& ramp = voice->layers[index].gain_ramp;
    m_impl->configure_gain_ramp(ramp, ramp.value(), gain_multipliers[index], duration_seconds);
  }
}

void PcmAudioMixer::set_voice_paused(VoiceToken token, bool paused) {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    throw std::logic_error("Cannot pause an inactive PCM voice");
  }
  voice->paused = paused;
}

void PcmAudioMixer::release_voice(VoiceToken token) {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice != m_impl->voices.end()) {
    m_impl->begin_release(*voice);
  }
}

void PcmAudioMixer::stop_voice(VoiceToken token) {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  std::erase_if(m_impl->voices, [token](const Impl::VoiceState& voice) { return voice.token == token; });
}

void PcmAudioMixer::stop_all_voices() {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->voices.clear();
}

bool PcmAudioMixer::is_voice_active(VoiceToken token) const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  return std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token) != m_impl->voices.end();
}

std::optional<float> PcmAudioMixer::voice_gain_multiplier(VoiceToken token) const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return std::nullopt;
  }
  return voice->gain_ramp.value();
}

std::optional<float> PcmAudioMixer::voice_pitch_multiplier(VoiceToken token) const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return std::nullopt;
  }
  return voice->pitch_multiplier;
}

std::optional<bool> PcmAudioMixer::voice_paused(VoiceToken token) const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return std::nullopt;
  }
  return voice->paused;
}

std::optional<std::uint64_t> PcmAudioMixer::voice_rendered_frames(VoiceToken token) const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->reclaim_finished_voices();
  const auto voice = std::ranges::find(m_impl->voices, token, &Impl::VoiceState::token);
  if (voice == m_impl->voices.end()) {
    return std::nullopt;
  }
  return voice->rendered_frames;
}

void PcmAudioMixer::render_interleaved(std::span<float> output) {
  if ((output.size() & 1U) != 0U) {
    throw std::invalid_argument("Interleaved stereo output must contain an even sample count");
  }
  const auto lock = std::scoped_lock(m_impl->mutex);
  m_impl->render_locked(output, false);
  m_impl->reclaim_finished_voices();
}

std::uint32_t PcmAudioMixer::output_sample_rate() const { return m_impl->output_rate; }

PlaybackStats PcmAudioMixer::stats() const {
  const auto lock = std::scoped_lock(m_impl->mutex);
  return m_impl->stats;
}

} // namespace aurora::audio
