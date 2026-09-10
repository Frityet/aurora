#include <aurora/audio.hpp>

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

int main() {
  using namespace aurora::audio;
  PcmAudioMixer mixer(4);
  const auto samples = std::make_shared<const std::vector<float>>(1, 1.0F);
  PcmVoiceSpec spec;
  spec.layers = {
      {.samples = samples, .sample_rate = 4, .loop_end = 1, .pan = 0.0F},
      {.samples = samples, .sample_rate = 4, .loop_end = 1, .pan = 1.0F},
  };
  const auto token = mixer.start_voice(spec);
  std::array<PcmLayerControls, 2> controls{{{0.25F, 1.0F}, {0.5F, 0.0F}}};
  assert(mixer.try_update_voice_controls(token, 2.0F, false, controls));
  std::array<float, 2> output;
  mixer.render_interleaved(output);
  assert(std::abs(output[0] - 0.5F) < 0.000001F && std::abs(output[1] - 0.25F) < 0.000001F);
  assert(mixer.voice_pitch_multiplier(token) == 2.0F && mixer.active_voice_count() == 1);
  const auto position = mixer.voice_rendered_frames(token);
  assert(mixer.try_update_voice_controls(token, 1.0F, true, controls));
  mixer.render_interleaved(output);
  assert(output[0] == 0 && output[1] == 0 && mixer.voice_rendered_frames(token) == position);
  bool invalid_count = false;
  try {
    (void)mixer.try_update_voice_controls(token, 1.0F, false, std::span(controls).first(1));
  } catch (const std::invalid_argument&) { invalid_count = true; }
  assert(invalid_count && mixer.voice_paused(token) == true);
  controls[0].gain = std::numeric_limits<float>::quiet_NaN();
  bool invalid_gain = false;
  try {
    (void)mixer.try_update_voice_controls(token, 1.0F, false, controls);
  } catch (const std::invalid_argument&) { invalid_gain = true; }
  assert(invalid_gain && mixer.voice_paused(token) == true);
  controls[0].gain = 0.25F;
  assert(mixer.try_update_voice_controls(token, 1.0F, false, controls));
  mixer.fade_out_voice(token, 0.5);
  assert(mixer.try_update_voice_controls(token, 1.0F, false, controls));
  std::array<float, 8> fade_output;
  mixer.render_interleaved(fade_output);
  assert(!mixer.is_voice_active(token) && mixer.active_voice_count() == 0);
  assert(!mixer.try_update_voice_controls(token, 1.0F, false, controls));
  std::puts(
      "[pass] atomic PCM channel volume/pan/pitch/pause, rejected-update isolation, release preservation and "
      "retired-token detection");
}
