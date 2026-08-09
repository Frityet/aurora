#include <aurora/audio.hpp>
#include <aurora/j_audio_sound_archive.hpp>
#include <aurora/j_audio_stream.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {

using aurora::audio::PcmAudioMixer;
using aurora::audio::PcmLayer;
using aurora::audio::PcmVoiceSpec;

std::shared_ptr<const std::vector<float>> samples(std::initializer_list<float> values) {
  return std::make_shared<const std::vector<float>>(values);
}

void put_be16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value >> 8U);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>(value);
}

void put_be32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  bytes.at(offset) = static_cast<std::uint8_t>(value >> 24U);
  bytes.at(offset + 1U) = static_cast<std::uint8_t>(value >> 16U);
  bytes.at(offset + 2U) = static_cast<std::uint8_t>(value >> 8U);
  bytes.at(offset + 3U) = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> finite_mono_stream() {
  auto bytes = std::vector<std::uint8_t>(0x64U);
  put_be32(bytes, 0x00U, 0x5354524dU); // STRM
  put_be32(bytes, 0x04U, 0x24U);
  bytes[0x09U] = 1U;
  put_be16(bytes, 0x0aU, 16U);
  put_be16(bytes, 0x0cU, 1U);
  put_be32(bytes, 0x10U, 4U);
  put_be32(bytes, 0x14U, 2U);
  put_be32(bytes, 0x20U, 4U);
  bytes[0x28U] = 127U;
  put_be32(bytes, 0x40U, 0x424c434bU); // BLCK
  put_be32(bytes, 0x44U, 4U);
  put_be16(bytes, 0x60U, 0x4000U);
  put_be16(bytes, 0x62U, 0xc000U);
  return bytes;
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
  EXPECT_EQ(mixer.voice_rendered_frames(token), 0U);

  mixer.set_voice_paused(token, false);
  auto resumed = std::array<float, 4>{};
  mixer.render_interleaved(resumed);
  EXPECT_FLOAT_EQ(resumed[0], 1.0F);
  EXPECT_FLOAT_EQ(resumed[2], 0.5F);
  EXPECT_EQ(mixer.voice_rendered_frames(token), 2U);
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

TEST(PcmAudioMixer, ScheduledGateReleasesALoopingSequenceLayer) {
  auto mixer = PcmAudioMixer{4U};
  const auto token = mixer.start_voice(PcmVoiceSpec{
      .layers = {PcmLayer{
          .samples = samples({1.0F}),
          .sample_rate = 4U,
          .loop_start = 0U,
          .loop_end = 1U,
          .pan = 0.0F,
          .gate_seconds = 0.5,
          .release_seconds = 0.5,
      }},
  });

  auto output = std::array<float, 8>{};
  mixer.render_interleaved(output);
  EXPECT_FLOAT_EQ(output[0], 1.0F);
  EXPECT_FLOAT_EQ(output[2], 1.0F);
  EXPECT_FLOAT_EQ(output[4], 1.0F);
  EXPECT_FLOAT_EQ(output[6], 0.5F);
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

TEST(JAudioStream, DecodesFiniteRetailPcm16BigEndianData) {
  const auto recipe = aurora::audio::decode_jaudio_stream(finite_mono_stream(), 0U);

  ASSERT_EQ(recipe.voice.layers.size(), 1U);
  EXPECT_FALSE(recipe.looping);
  EXPECT_EQ(recipe.sample_rate, 4U);
  EXPECT_EQ(recipe.sample_count, 2U);
  EXPECT_EQ(recipe.voice.layers[0].loop_end, 0U);
  EXPECT_FLOAT_EQ(recipe.voice.layers[0].gain, 1.0F);
  EXPECT_FLOAT_EQ(recipe.voice.layers[0].pan, 0.5F);
  EXPECT_EQ(*recipe.voice.layers[0].samples, (std::vector<float>{0.5F, -0.5F}));
}

TEST(JAudioStream, RejectsTruncatedBlockPayload) {
  auto bytes = finite_mono_stream();
  bytes.pop_back();
  EXPECT_THROW(static_cast<void>(aurora::audio::decode_jaudio_stream(bytes, 0U)), std::runtime_error);
}

TEST(JAudioStream, RejectsImpossibleSampleCountBeforeAllocation) {
  auto bytes = finite_mono_stream();
  put_be32(bytes, 0x14U, std::numeric_limits<std::uint32_t>::max());
  EXPECT_THROW(static_cast<void>(aurora::audio::decode_jaudio_stream(bytes, 0U)), std::runtime_error);
}

TEST(JAudioSoundArchive, RejectsArchiveWithoutRequiredRetailTables) {
  const auto baa = std::array<std::uint8_t, 8U>{
      0x41U, 0x41U, 0x5fU, 0x3cU, 0x3eU, 0x5fU, 0x41U, 0x41U,
  };
  EXPECT_THROW(static_cast<void>(aurora::audio::JAudioSoundArchive{
                   baa, [](std::string_view) { return std::vector<std::uint8_t>{}; }}),
               std::runtime_error);
}

} // namespace
