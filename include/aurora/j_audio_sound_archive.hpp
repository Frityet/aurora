#pragma once

#include <aurora/audio.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace aurora::audio {

enum class JAudioSoundKind {
  SoundEffect,
  Sequence,
  Stream,
};

struct JAudioSoundMetadata {
  std::uint32_t sound_id = 0;
  JAudioSoundKind kind = JAudioSoundKind::SoundEffect;
  std::uint8_t priority = 0;
  std::uint8_t volume = 0;
  std::uint16_t resource_id = 0;
  std::uint16_t chord_resource_id = 0;
  std::uint16_t channel_control = 0;
  std::string stream_path;
};

struct JAudioSoundLayerAudit {
  std::uint8_t bank = 0;
  std::uint8_t program = 0;
  std::uint8_t note = 0;
  std::uint8_t velocity = 0;
  std::uint16_t wave_id = 0;
  std::string wave_archive_name;
  std::uint32_t wave_archive_offset = 0;
  std::uint32_t encoded_length = 0;
  std::uint32_t decoded_loop_start = 0;
  std::uint32_t decoded_loop_end = 0;
  std::uint32_t source_sample_rate = 0;
  std::uint32_t decoded_sample_count = 0;
  std::uint16_t direct_release_ticks = 0;
  std::int16_t loop_history_yn1 = 0;
  std::int16_t loop_history_yn2 = 0;
  double start_delay_seconds = 0.0;
  double gate_seconds = 0.0;
  bool waits_for_sample_completion = false;
  bool looping = false;
};

struct JAudioPersistentSoundRecipe {
  std::uint32_t sound_id = 0;
  std::uint8_t priority = 0;
  std::uint8_t table_volume = 0;
  PcmVoiceSpec voice;
  std::vector<JAudioSoundLayerAudit> layers;
};

struct JAudioSoundEffectRecipe {
  std::uint32_t sound_id = 0;
  std::uint8_t priority = 0;
  std::uint8_t table_volume = 0;
  PcmVoiceSpec voice;
  std::vector<JAudioSoundLayerAudit> layers;
};

// Bounds-checked reader for JAudio's BAA/BST/BSTN/BSC/IBNK/WSYS chain.
// Wave archive data is supplied by name so the parser is independent of
// the host/DVD filesystem used by the compatibility layer.
class JAudioSoundArchive final {
public:
  using WaveArchiveLoader = std::function<std::vector<std::uint8_t>(std::string_view)>;

  JAudioSoundArchive(std::span<const std::uint8_t> decompressed_baa, WaveArchiveLoader wave_archive_loader);
  ~JAudioSoundArchive();

  JAudioSoundArchive(const JAudioSoundArchive&) = delete;
  JAudioSoundArchive& operator=(const JAudioSoundArchive&) = delete;
  JAudioSoundArchive(JAudioSoundArchive&&) noexcept;
  JAudioSoundArchive& operator=(JAudioSoundArchive&&) noexcept;

  [[nodiscard]] std::optional<std::uint32_t> find_sound_id(std::string_view name) const;
  [[nodiscard]] std::optional<JAudioSoundMetadata> resolve_sound(std::string_view name) const;
  [[nodiscard]] JAudioSoundMetadata resolve_sound(std::uint32_t sound_id) const;
  [[nodiscard]] std::optional<JAudioPersistentSoundRecipe> resolve_persistent_sound(std::string_view name) const;
  [[nodiscard]] std::optional<JAudioSoundEffectRecipe> resolve_sound_effect(std::string_view name) const;
  [[nodiscard]] JAudioSoundEffectRecipe resolve_sound_effect(std::uint32_t sound_id) const;

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
};

} // namespace aurora::audio
