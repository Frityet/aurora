#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

#include <dolphin/gx.h>

namespace aurora::rfl::detail {

struct TextureResourceDescriptor {
  GXTexFmt format = GX_TF_I4;
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  GXTexWrapMode wrap_s = GX_CLAMP;
  GXTexWrapMode wrap_t = GX_CLAMP;
  bool edge_lod = false;
  bool bias_clamp = false;
  GXAnisotropy max_anisotropy = GX_ANISO_1;
  GXTexFilter min_filter = GX_NEAR;
  GXTexFilter mag_filter = GX_NEAR;
  float min_lod = 0.0F;
  float max_lod = 0.0F;
  float lod_bias = 0.0F;
  std::size_t image_offset = 0;
  std::size_t image_size = 0;
};

[[nodiscard]] std::optional<std::int32_t> to_fixed_8(float value) noexcept;
[[nodiscard]] bool is_supported_shape_primitive(std::uint8_t value) noexcept;
[[nodiscard]] std::optional<TextureResourceDescriptor>
parse_texture_resource(std::span<const std::uint8_t> bytes) noexcept;

} // namespace aurora::rfl::detail
