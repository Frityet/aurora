#include "CharacterResource.hpp"

#include <cmath>
#include <limits>

namespace aurora::rfl::detail {
namespace {

[[nodiscard]] std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

[[nodiscard]] std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] bool contains_range(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) noexcept {
  return offset <= bytes.size() && size <= bytes.size() - offset;
}

[[nodiscard]] constexpr bool valid_texture_format(std::uint8_t value) noexcept {
  switch (static_cast<GXTexFmt>(value)) {
  case GX_TF_I4:
  case GX_TF_I8:
  case GX_TF_IA4:
  case GX_TF_IA8:
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
  case GX_TF_RGBA8:
  case GX_TF_CMPR:
    return true;
  default:
    return false;
  }
}

[[nodiscard]] constexpr std::size_t texture_image_size(GXTexFmt format, std::uint16_t width,
                                                       std::uint16_t height) noexcept {
  std::size_t block_width = 4;
  std::size_t block_height = 4;
  std::size_t bytes_per_block = 32;
  switch (format) {
  case GX_TF_I4:
  case GX_TF_CMPR:
    block_width = 8;
    block_height = 8;
    break;
  case GX_TF_I8:
  case GX_TF_IA4:
    block_width = 8;
    break;
  case GX_TF_RGBA8:
    bytes_per_block = 64;
    break;
  case GX_TF_IA8:
  case GX_TF_RGB565:
  case GX_TF_RGB5A3:
    break;
  default:
    return 0;
  }
  const auto blocks_x = (static_cast<std::size_t>(width) + block_width - 1) / block_width;
  const auto blocks_y = (static_cast<std::size_t>(height) + block_height - 1) / block_height;
  return blocks_x * blocks_y * bytes_per_block;
}

} // namespace

std::optional<std::int32_t> to_fixed_8(float value) noexcept {
  if (!std::isfinite(value)) {
    return std::nullopt;
  }
  const auto scaled = static_cast<double>(value) * 256.0;
  if (scaled < static_cast<double>(std::numeric_limits<std::int32_t>::min()) ||
      scaled > static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
    return std::nullopt;
  }
  return static_cast<std::int32_t>(scaled);
}

bool is_supported_shape_primitive(std::uint8_t value) noexcept {
  switch (static_cast<GXPrimitive>(value)) {
  case GX_QUADS:
  case GX_TRIANGLES:
  case GX_TRIANGLESTRIP:
  case GX_TRIANGLEFAN:
  case GX_LINES:
  case GX_LINESTRIP:
  case GX_POINTS:
    return true;
  }
  return false;
}

std::optional<TextureResourceDescriptor> parse_texture_resource(std::span<const std::uint8_t> bytes) noexcept {
  if (bytes.size() < 0x20 || !valid_texture_format(bytes[0])) {
    return std::nullopt;
  }

  constexpr auto max_texture_dimension = std::uint16_t{1024};
  TextureResourceDescriptor descriptor{
      .format = static_cast<GXTexFmt>(bytes[0]),
      .width = read_be16(bytes, 2),
      .height = read_be16(bytes, 4),
      .wrap_s = static_cast<GXTexWrapMode>(bytes[6]),
      .wrap_t = static_cast<GXTexWrapMode>(bytes[7]),
      .edge_lod = bytes[0x11] != 0,
      .bias_clamp = bytes[0x12] != 0,
      .max_anisotropy = static_cast<GXAnisotropy>(bytes[0x13]),
      .min_filter = static_cast<GXTexFilter>(bytes[0x14]),
      .mag_filter = static_cast<GXTexFilter>(bytes[0x15]),
      .min_lod = static_cast<float>(static_cast<std::int8_t>(bytes[0x16])),
      .max_lod = static_cast<float>(static_cast<std::int8_t>(bytes[0x17])),
      .lod_bias = static_cast<float>(static_cast<std::int16_t>(read_be16(bytes, 0x1A))),
      .image_offset = static_cast<std::size_t>(read_be32(bytes, 0x1C)),
  };
  const auto indexed = bytes[8] != 0;
  if (indexed || descriptor.width == 0 || descriptor.width > max_texture_dimension || descriptor.height == 0 ||
      descriptor.height > max_texture_dimension || bytes[6] >= static_cast<std::uint8_t>(GX_MAX_TEXWRAPMODE) ||
      bytes[7] >= static_cast<std::uint8_t>(GX_MAX_TEXWRAPMODE) || bytes[0x11] > 1 || bytes[0x12] > 1 ||
      bytes[0x13] >= static_cast<std::uint8_t>(GX_MAX_ANISOTROPY) ||
      bytes[0x14] > static_cast<std::uint8_t>(GX_LIN_MIP_LIN) || bytes[0x15] > static_cast<std::uint8_t>(GX_LINEAR) ||
      descriptor.image_offset < 0x20 || descriptor.image_offset >= bytes.size()) {
    return std::nullopt;
  }

  descriptor.image_size = texture_image_size(descriptor.format, descriptor.width, descriptor.height);
  if (descriptor.image_size == 0 || !contains_range(bytes, descriptor.image_offset, descriptor.image_size)) {
    return std::nullopt;
  }
  return descriptor;
}

} // namespace aurora::rfl::detail
