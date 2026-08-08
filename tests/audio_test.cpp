#include <aurora/audio.hpp>

#include <gtest/gtest.h>

#include <array>
#include <memory>
#include <vector>

namespace {

using aurora::audio::PcmAudioMixer;
using aurora::audio::PcmLayer;
using aurora::audio::PcmVoiceSpec;

std::shared_ptr<const std::vector<float>> samples(std::initializer_list<float> values) {
  return std::make_shared<const std::vector<float>>(values);
}

TEST(PcmAudioMixer, FiniteLayerRetiresAfterItsLastSample) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F, 0.5F}),
          .sample_rate = 4U,
          .pan = 0.0F,
      }},
  });

  auto output = std::array<float, 8>{};
  mixer.render_interleaved(output);

  EXPECT_FLOAT_EQ(output[0], 1.0F);
  EXPECT_FLOAT_EQ(output[2], 0.5F);
  EXPECT_FLOAT_EQ(output[4], 0.0F);
  EXPECT_FLOAT_EQ(output[6], 0.0F);
  EXPECT_FALSE(mixer.is_voice_active(token));
}

TEST(PcmAudioMixer, AppliesNonUnitInitialVoiceGain) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F}),
          .sample_rate = 4U,
          .loop_start = 0U,
          .loop_end = 1U,
          .pan = 0.0F,
      }},
      .gain_multiplier = 0.25F,
  });

  auto output = std::array<float, 2>{};
  mixer.render_interleaved(output);
  EXPECT_FLOAT_EQ(output[0], 0.25F);
  EXPECT_EQ(mixer.voice_gain_multiplier(token), 0.25F);
}

TEST(PcmAudioMixer, ResamplesFiniteLayerBeforeRetiringIt) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F, 0.0F, -1.0F}),
          .sample_rate = 2U,
          .pan = 0.0F,
      }},
  });

  auto output = std::array<float, 12>{};
  mixer.render_interleaved(output);
  EXPECT_FLOAT_EQ(output[0], 1.0F);
  EXPECT_FLOAT_EQ(output[2], 0.5F);
  EXPECT_FLOAT_EQ(output[4], 0.0F);
  EXPECT_FLOAT_EQ(output[6], -0.5F);
  EXPECT_FLOAT_EQ(output[8], -1.0F);
  EXPECT_FLOAT_EQ(output[10], -1.0F);
  EXPECT_FALSE(mixer.is_voice_active(token));
}

TEST(PcmAudioMixer, PauseFreezesFiniteSourcePosition) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F, 0.5F, 0.25F}),
          .sample_rate = 4U,
          .pan = 0.0F,
      }},
  });

  mixer.set_voice_paused(token, true);
  auto paused = std::array<float, 4>{1.0F, 1.0F, 1.0F, 1.0F};
  mixer.render_interleaved(paused);
  EXPECT_EQ(paused, (std::array<float, 4>{}));
  EXPECT_EQ(mixer.voice_paused(token), true);

  mixer.set_voice_paused(token, false);
  auto resumed = std::array<float, 4>{};
  mixer.render_interleaved(resumed);
  EXPECT_FLOAT_EQ(resumed[0], 1.0F);
  EXPECT_FLOAT_EQ(resumed[2], 0.5F);
}

TEST(PcmAudioMixer, FadeOutUsesRenderedFramesAndRetiresVoice) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F}),
          .sample_rate = 4U,
          .loop_start = 0U,
          .loop_end = 1U,
          .pan = 0.0F,
      }},
  });

  mixer.fade_out_voice(token, 1.0);
  EXPECT_EQ(mixer.voice_gain_multiplier(token), 1.0F);
  auto output = std::array<float, 8>{};
  mixer.render_interleaved(output);

  EXPECT_FLOAT_EQ(output[0], 1.0F);
  EXPECT_FLOAT_EQ(output[2], 0.75F);
  EXPECT_FLOAT_EQ(output[4], 0.5F);
  EXPECT_FLOAT_EQ(output[6], 0.25F);
  EXPECT_FALSE(mixer.is_voice_active(token));
}

TEST(PcmAudioMixer, LayerGainTransitionControlsChannelsIndependently) {
  auto mixer = PcmAudioMixer{4U};
  const auto constant = samples({1.0F});
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers =
          {
              PcmLayer{
                  .samples = constant,
                  .sample_rate = 4U,
                  .loop_start = 0U,
                  .loop_end = 1U,
                  .pan = 0.0F,
              },
              PcmLayer{
                  .samples = constant,
                  .sample_rate = 4U,
                  .loop_start = 0U,
                  .loop_end = 1U,
                  .pan = 1.0F,
              },
          },
  });

  const auto gains = std::array{0.25F, 0.5F};
  mixer.fade_layer_gains(token, gains, 0.0);
  auto output = std::array<float, 2>{};
  mixer.render_interleaved(output);

  EXPECT_NEAR(output[0], 0.25F, 1.0e-6F);
  EXPECT_NEAR(output[1], 0.5F, 1.0e-6F);
}

TEST(PcmAudioMixer, RejectsHalfSpecifiedLoopRange) {
  auto mixer = PcmAudioMixer{4U};
  EXPECT_THROW(static_cast<void>(mixer.start_voice(PcmVoiceSpec{
                   .layers = {PcmLayer{
                       .samples = samples({1.0F, 0.5F}),
                       .sample_rate = 4U,
                       .loop_start = 1U,
                   }},
               })),
               std::invalid_argument);
}

} // namespace
