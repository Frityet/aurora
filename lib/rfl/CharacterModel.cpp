#include <aurora/rfl/CharacterModel.hpp>

#include "CharacterResource.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/mtx.h>

namespace aurora::rfl {
namespace {

constexpr auto expression_count = static_cast<std::size_t>(RFLExp_Max);
constexpr auto expression_mask = (std::uint32_t{1} << expression_count) - 1;
constexpr auto shape_count = std::size_t{9};
constexpr auto pi = 3.14159265358979323846F;

enum class ShapePart : std::uint8_t {
  Nose,
  Forehead,
  Faceline,
  Hair,
  Cap,
  Beard,
  NoseLine,
  Mask,
  Glass,
};

enum class PartTexture : std::uint8_t {
  Eye,
  Eyebrow,
  Mouth,
  Mustache,
  Mole,
};

enum class ShapeTexture : std::uint8_t {
  Face,
  Cap,
  NoseLine,
  Glass,
};

enum class QuadOrigin : std::uint8_t {
  Center,
  Right,
  Left,
};

struct Vec3 {
  float x = 0.0F;
  float y = 0.0F;
  float z = 0.0F;
};

struct CharacterInfo {
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t texture = 0;
  } faceline;
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    bool flip = false;
  } hair;
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t scale = 0;
    std::uint8_t rotate = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
  } eye;
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t scale = 0;
    std::uint8_t rotate = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
  } eyebrow;
  struct {
    std::uint8_t type = 0;
    std::uint8_t scale = 0;
    std::uint8_t y = 0;
  } nose;
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t scale = 0;
    std::uint8_t y = 0;
  } mouth;
  struct {
    std::uint8_t mustache = 0;
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t scale = 0;
    std::uint8_t y = 0;
  } beard;
  struct {
    std::uint8_t type = 0;
    std::uint8_t color = 0;
    std::uint8_t scale = 0;
    std::uint8_t y = 0;
  } glass;
  struct {
    std::uint8_t type = 0;
    std::uint8_t scale = 0;
    std::uint8_t x = 0;
    std::uint8_t y = 0;
  } mole;
  std::uint8_t favorite_color = 0;
};

struct Shape {
  std::vector<std::int16_t> positions;
  std::vector<std::int16_t> normals;
  std::vector<std::int16_t> texcoords;
  std::vector<std::uint8_t> display_list;
  std::uint32_t display_list_size = 0;
  std::size_t primitive_count = 0;

  [[nodiscard]] bool empty() const { return display_list_size == 0; }
};

struct Texture {
  std::vector<std::uint8_t> resource;
  GXTexObj object{};
  std::uint16_t width = 0;
  std::uint16_t height = 0;
  GXTexFmt format = GX_TF_I4;
  const std::uint8_t* image = nullptr;

  Texture() = default;
  Texture(const Texture&) = delete;
  Texture& operator=(const Texture&) = delete;
  Texture(Texture&&) = delete;
  Texture& operator=(Texture&&) = delete;
  ~Texture() { GXDestroyTexObj(&object); }
};

struct FacePart {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;
  float angle = 0.0F;
  QuadOrigin origin = QuadOrigin::Center;
  Texture* texture = nullptr;
};

struct FaceParts {
  FacePart right_eye;
  FacePart left_eye;
  FacePart right_eyebrow;
  FacePart left_eyebrow;
  FacePart mouth;
  FacePart right_mustache;
  FacePart left_mustache;
  FacePart mole;
};

struct ShapeBuildSpec {
  ShapePart part = ShapePart::Faceline;
  std::uint16_t file = 0;
  bool transform = false;
  bool flip_x = false;
  float scale = 1.0F;
  Vec3 translation{};
  Vec3* nose_translation = nullptr;
  Vec3* beard_translation = nullptr;
  Vec3* hair_translation = nullptr;
};

constexpr auto default_character_data = std::array{
    std::array<std::uint8_t, 74>{
        0x00, 0x08, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x00, 0xEC, 0xFF,
        0x82, 0xD2, 0x10, 0x04, 0x88, 0x00, 0x31, 0x80, 0x08, 0xA2, 0x08, 0x8C, 0x08, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    std::array<std::uint8_t, 74>{
        0x00, 0x0A, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x01, 0xEC, 0xFF,
        0x82, 0xD2, 0x00, 0x04, 0x6F, 0x80, 0x31, 0x80, 0xC8, 0xA2, 0x08, 0x8C, 0x88, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    std::array<std::uint8_t, 74>{
        0x00, 0x00, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x02, 0xEC, 0xFF,
        0x82, 0xD2, 0x04, 0x04, 0x42, 0x40, 0x31, 0x80, 0x28, 0xA2, 0x08, 0x8C, 0x08, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    std::array<std::uint8_t, 74>{
        0x40, 0x04, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x03, 0xEC, 0xFF,
        0x82, 0xD2, 0x08, 0x04, 0x30, 0x00, 0x01, 0x80, 0x08, 0xA2, 0x10, 0x6C, 0x08, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    std::array<std::uint8_t, 74>{
        0x40, 0x0C, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x04, 0xEC, 0xFF,
        0x82, 0xD2, 0x00, 0x04, 0x1D, 0xC0, 0x01, 0x80, 0xE8, 0xA2, 0x10, 0x6C, 0xA8, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
    std::array<std::uint8_t, 74>{
        0x40, 0x0E, 0x00, 0x6E, 0x00, 0x6F, 0x00, 0x20, 0x00, 0x6E, 0x00, 0x61, 0x00, 0x6D, 0x00,
        0x65, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x40, 0x80, 0x00, 0x00, 0x05, 0xEC, 0xFF,
        0x82, 0xD2, 0x00, 0x04, 0x18, 0x40, 0x01, 0x80, 0x28, 0xA2, 0x10, 0x6C, 0x08, 0x58, 0x14,
        0x4A, 0xB8, 0x8D, 0x00, 0x8A, 0x00, 0x8A, 0x25, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    },
};

constexpr auto faceline_colors = std::array{
    GXColor{240, 216, 196, 255}, GXColor{255, 188, 128, 255}, GXColor{216, 136, 80, 255},
    GXColor{255, 176, 144, 255}, GXColor{152, 80, 48, 255},   GXColor{82, 46, 28, 255},
};

constexpr auto hair_colors = std::array{
    GXColor{30, 26, 24, 255},    GXColor{56, 32, 21, 255}, GXColor{85, 38, 23, 255},  GXColor{112, 64, 36, 255},
    GXColor{114, 114, 120, 255}, GXColor{73, 54, 26, 255}, GXColor{122, 89, 40, 255}, GXColor{193, 159, 100, 255},
};

constexpr auto glass_colors = std::array{
    GXColor{16, 16, 16, 255}, GXColor{96, 56, 16, 255}, GXColor{152, 24, 16, 255},
    GXColor{32, 48, 96, 255}, GXColor{144, 88, 0, 255}, GXColor{96, 88, 80, 255},
};

constexpr auto favorite_colors = std::array{
    GXColor{184, 64, 48, 255},  GXColor{240, 120, 40, 255}, GXColor{248, 216, 32, 255},  GXColor{128, 200, 40, 255},
    GXColor{0, 116, 40, 255},   GXColor{32, 72, 152, 255},  GXColor{64, 160, 216, 255},  GXColor{232, 96, 120, 255},
    GXColor{112, 44, 168, 255}, GXColor{72, 56, 24, 255},   GXColor{224, 224, 224, 255}, GXColor{24, 24, 20, 255},
};

constexpr auto eye_colors = std::array{
    GXColor{0, 0, 0, 255},      GXColor{124, 128, 128, 255}, GXColor{112, 80, 64, 255},
    GXColor{112, 110, 64, 255}, GXColor{88, 104, 184, 255},  GXColor{72, 128, 104, 255},
};

constexpr auto eyebrow_colors = hair_colors;
constexpr auto beard_colors = hair_colors;

constexpr auto mouth_colors0 = std::array{
    GXColor{190, 78, 38, 255},
    GXColor{216, 48, 40, 255},
    GXColor{207, 68, 71, 255},
};
constexpr auto mouth_colors1 = std::array{
    GXColor{113, 42, 4, 255},
    GXColor{120, 21, 16, 255},
    GXColor{126, 37, 40, 255},
};

constexpr auto eye_rotation_offsets = std::array<std::uint8_t, 50>{
    29, 28, 28, 28, 29, 28, 28, 28, 29, 28, 28, 28, 28, 29, 29, 28, 28, 28, 29, 29, 28, 29, 28, 29, 29,
    28, 29, 28, 28, 29, 28, 28, 28, 29, 29, 29, 28, 28, 29, 29, 29, 28, 28, 29, 29, 29, 29, 29, 28, 28,
};

constexpr auto eyebrow_rotation_offsets = std::array<std::uint8_t, 24>{
    26, 26, 27, 25, 26, 25, 26, 25, 28, 25, 26, 24, 27, 27, 26, 26, 25, 25, 26, 26, 27, 26, 25, 27,
};

[[nodiscard]] constexpr std::size_t shape_index(ShapePart part) { return static_cast<std::size_t>(part); }

[[nodiscard]] std::uint16_t read_be16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[offset]) << 8U) | bytes[offset + 1U]);
}

[[nodiscard]] std::uint32_t read_be32(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
         (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
}

[[nodiscard]] float read_be_float(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return std::bit_cast<float>(read_be32(bytes, offset));
}

[[nodiscard]] constexpr std::uint16_t extract_msb(std::uint16_t value, unsigned offset, unsigned width) {
  return static_cast<std::uint16_t>((value >> (16U - offset - width)) & ((1U << width) - 1U));
}

[[nodiscard]] CharacterInfo decode_default_character(std::span<const std::uint8_t, 74> bytes) {
  CharacterInfo info;
  const auto personal = read_be16(bytes, 0x00);
  const auto faceline = read_be16(bytes, 0x20);
  const auto hair = read_be16(bytes, 0x22);
  const auto eyebrow0 = read_be16(bytes, 0x24);
  const auto eyebrow1 = read_be16(bytes, 0x26);
  const auto eye0 = read_be16(bytes, 0x28);
  const auto eye1 = read_be16(bytes, 0x2A);
  const auto nose = read_be16(bytes, 0x2C);
  const auto mouth = read_be16(bytes, 0x2E);
  const auto glass = read_be16(bytes, 0x30);
  const auto beard = read_be16(bytes, 0x32);
  const auto mole = read_be16(bytes, 0x34);

  info.faceline.type = static_cast<std::uint8_t>(extract_msb(faceline, 0, 3));
  info.faceline.color = static_cast<std::uint8_t>(extract_msb(faceline, 3, 3));
  info.faceline.texture = static_cast<std::uint8_t>(extract_msb(faceline, 6, 4));
  info.hair.type = static_cast<std::uint8_t>(extract_msb(hair, 0, 7));
  info.hair.color = static_cast<std::uint8_t>(extract_msb(hair, 7, 3));
  info.hair.flip = extract_msb(hair, 10, 1) != 0;
  info.eyebrow.type = static_cast<std::uint8_t>(extract_msb(eyebrow0, 0, 5));
  info.eyebrow.rotate = static_cast<std::uint8_t>(extract_msb(eyebrow0, 5, 5));
  info.eyebrow.color = static_cast<std::uint8_t>(extract_msb(eyebrow1, 0, 3));
  info.eyebrow.scale = static_cast<std::uint8_t>(extract_msb(eyebrow1, 3, 4));
  info.eyebrow.y = static_cast<std::uint8_t>(extract_msb(eyebrow1, 7, 5));
  info.eyebrow.x = static_cast<std::uint8_t>(extract_msb(eyebrow1, 12, 4));
  info.eye.type = static_cast<std::uint8_t>(extract_msb(eye0, 0, 6));
  info.eye.rotate = static_cast<std::uint8_t>(extract_msb(eye0, 6, 5));
  info.eye.y = static_cast<std::uint8_t>(extract_msb(eye0, 11, 5));
  info.eye.color = static_cast<std::uint8_t>(extract_msb(eye1, 0, 3));
  info.eye.scale = static_cast<std::uint8_t>(extract_msb(eye1, 3, 4));
  info.eye.x = static_cast<std::uint8_t>(extract_msb(eye1, 7, 4));
  info.nose.type = static_cast<std::uint8_t>(extract_msb(nose, 0, 4));
  info.nose.scale = static_cast<std::uint8_t>(extract_msb(nose, 4, 4));
  info.nose.y = static_cast<std::uint8_t>(extract_msb(nose, 8, 5));
  info.mouth.type = static_cast<std::uint8_t>(extract_msb(mouth, 0, 5));
  info.mouth.color = static_cast<std::uint8_t>(extract_msb(mouth, 5, 2));
  info.mouth.scale = static_cast<std::uint8_t>(extract_msb(mouth, 7, 4));
  info.mouth.y = static_cast<std::uint8_t>(extract_msb(mouth, 11, 5));
  info.glass.type = static_cast<std::uint8_t>(extract_msb(glass, 0, 4));
  info.glass.color = static_cast<std::uint8_t>(extract_msb(glass, 4, 3));
  info.glass.scale = static_cast<std::uint8_t>(extract_msb(glass, 7, 4));
  info.glass.y = static_cast<std::uint8_t>(extract_msb(glass, 11, 5));
  info.beard.mustache = static_cast<std::uint8_t>(extract_msb(beard, 0, 2));
  info.beard.type = static_cast<std::uint8_t>(extract_msb(beard, 2, 2));
  info.beard.color = static_cast<std::uint8_t>(extract_msb(beard, 4, 3));
  info.beard.scale = static_cast<std::uint8_t>(extract_msb(beard, 7, 4));
  info.beard.y = static_cast<std::uint8_t>(extract_msb(beard, 11, 5));
  info.mole.type = static_cast<std::uint8_t>(extract_msb(mole, 0, 1));
  info.mole.scale = static_cast<std::uint8_t>(extract_msb(mole, 1, 4));
  info.mole.y = static_cast<std::uint8_t>(extract_msb(mole, 5, 5));
  info.mole.x = static_cast<std::uint8_t>(extract_msb(mole, 10, 5));
  info.favorite_color = static_cast<std::uint8_t>(extract_msb(personal, 11, 4));
  return info;
}

[[nodiscard]] constexpr ResourceArchiveId shape_archive(ShapePart part) {
  constexpr auto archives = std::array{
      ResourceArchiveId::Nose,     ResourceArchiveId::Forehead, ResourceArchiveId::Faceline,
      ResourceArchiveId::Hair,     ResourceArchiveId::Cap,      ResourceArchiveId::Beard,
      ResourceArchiveId::NoseLine, ResourceArchiveId::Mask,     ResourceArchiveId::Glass,
  };
  return archives[shape_index(part)];
}

[[nodiscard]] constexpr std::array<std::uint8_t, 4> shape_magic(ShapePart part) {
  constexpr auto magic = std::array{
      std::array<std::uint8_t, 4>{'n', 'o', 's', 'e'}, std::array<std::uint8_t, 4>{'f', 'r', 'h', 'd'},
      std::array<std::uint8_t, 4>{'f', 'a', 'c', 'e'}, std::array<std::uint8_t, 4>{'h', 'a', 'i', 'r'},
      std::array<std::uint8_t, 4>{'c', 'a', 'p', '_'}, std::array<std::uint8_t, 4>{'b', 'e', 'r', 'd'},
      std::array<std::uint8_t, 4>{'n', 's', 'l', 'n'}, std::array<std::uint8_t, 4>{'m', 'a', 's', 'k'},
      std::array<std::uint8_t, 4>{'g', 'l', 'a', 's'},
  };
  return magic[shape_index(part)];
}

[[nodiscard]] constexpr bool shape_has_texcoords(ShapePart part) {
  return part != ShapePart::Forehead && part != ShapePart::Hair && part != ShapePart::Beard && part != ShapePart::Nose;
}

[[nodiscard]] constexpr ResourceArchiveId part_texture_archive(PartTexture part) {
  constexpr auto archives = std::array{
      ResourceArchiveId::Eye,      ResourceArchiveId::Eyebrow, ResourceArchiveId::Mouth,
      ResourceArchiveId::Mustache, ResourceArchiveId::Mole,
  };
  return archives[static_cast<std::size_t>(part)];
}

[[nodiscard]] constexpr ResourceArchiveId shape_texture_archive(ShapeTexture part) {
  constexpr auto archives = std::array{
      ResourceArchiveId::FaceTexture,
      ResourceArchiveId::CapTexture,
      ResourceArchiveId::NoseLineTexture,
      ResourceArchiveId::GlassTexture,
  };
  return archives[static_cast<std::size_t>(part)];
}

[[nodiscard]] bool has_range(std::span<const std::uint8_t> bytes, std::size_t offset, std::size_t size) {
  return offset <= bytes.size() && size <= bytes.size() - offset;
}

[[nodiscard]] std::int16_t read_be_s16(std::span<const std::uint8_t> bytes, std::size_t offset) {
  return static_cast<std::int16_t>(read_be16(bytes, offset));
}

[[nodiscard]] bool read_vec3(std::span<const std::uint8_t> bytes, std::size_t& offset, Vec3& value) {
  if (!has_range(bytes, offset, 12)) {
    return false;
  }
  value.x = read_be_float(bytes, offset);
  value.y = read_be_float(bytes, offset + 4);
  value.z = read_be_float(bytes, offset + 8);
  offset += 12;
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] std::optional<Shape> build_shape(const ResourceArchive& archive, const ShapeBuildSpec& spec,
                                               CharacterModelError& error) {
  const auto file = archive.file(shape_archive(spec.part), spec.file);
  if (!file.has_value()) {
    error = CharacterModelError::ResourceMissing;
    return std::nullopt;
  }
  const auto bytes = *file;
  const auto expected_magic = shape_magic(spec.part);
  if (!has_range(bytes, 0, 6) || !std::equal(expected_magic.begin(), expected_magic.end(), bytes.begin())) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }

  auto offset = std::size_t{4};
  if (spec.part == ShapePart::Faceline) {
    if (spec.nose_translation == nullptr || spec.beard_translation == nullptr || spec.hair_translation == nullptr ||
        !read_vec3(bytes, offset, *spec.nose_translation) || !read_vec3(bytes, offset, *spec.beard_translation) ||
        !read_vec3(bytes, offset, *spec.hair_translation)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
  }

  if (!has_range(bytes, offset, 2)) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }
  const auto position_count = static_cast<std::size_t>(read_be16(bytes, offset));
  offset += 2;
  auto shape = Shape{};
  if (position_count == 0) {
    const auto empty_tail_size = shape_has_texcoords(spec.part) ? std::size_t{5} : std::size_t{3};
    if (!has_range(bytes, offset, empty_tail_size) || offset + empty_tail_size != bytes.size() ||
        read_be16(bytes, offset) != 0 || (shape_has_texcoords(spec.part) && read_be16(bytes, offset + 2) != 0) ||
        bytes.back() != 0) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    return shape;
  }
  if (position_count > 255 || !has_range(bytes, offset, position_count * 6)) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }

  shape.positions.resize(position_count * 3);
  const auto scale = detail::to_fixed_8(spec.scale);
  const auto translation_x = detail::to_fixed_8(spec.translation.x);
  const auto translation_y = detail::to_fixed_8(spec.translation.y);
  const auto translation_z = detail::to_fixed_8(spec.translation.z);
  if (!scale.has_value() || !translation_x.has_value() || !translation_y.has_value() || !translation_z.has_value()) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }
  for (auto vertex = std::size_t{}; vertex < position_count; ++vertex) {
    auto x = static_cast<std::int64_t>(read_be_s16(bytes, offset));
    auto y = static_cast<std::int64_t>(read_be_s16(bytes, offset + 2));
    auto z = static_cast<std::int64_t>(read_be_s16(bytes, offset + 4));
    offset += 6;
    if (spec.flip_x) {
      x = -x;
    }
    if (spec.transform) {
      x = *translation_x + ((x * *scale) >> 8);
      y = *translation_y + ((y * *scale) >> 8);
      z = *translation_z + ((z * *scale) >> 8);
    }
    if (x < std::numeric_limits<std::int16_t>::min() || x > std::numeric_limits<std::int16_t>::max() ||
        y < std::numeric_limits<std::int16_t>::min() || y > std::numeric_limits<std::int16_t>::max() ||
        z < std::numeric_limits<std::int16_t>::min() || z > std::numeric_limits<std::int16_t>::max()) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    shape.positions[vertex * 3] = static_cast<std::int16_t>(x);
    shape.positions[vertex * 3 + 1] = static_cast<std::int16_t>(y);
    shape.positions[vertex * 3 + 2] = static_cast<std::int16_t>(z);
  }

  if (!has_range(bytes, offset, 2)) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }
  const auto normal_count = static_cast<std::size_t>(read_be16(bytes, offset));
  offset += 2;
  if (normal_count > 255 || !has_range(bytes, offset, normal_count * 6)) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }
  shape.normals.resize(normal_count * 3);
  for (auto vertex = std::size_t{}; vertex < normal_count; ++vertex) {
    auto x = static_cast<std::int32_t>(read_be_s16(bytes, offset));
    const auto y = read_be_s16(bytes, offset + 2);
    const auto z = read_be_s16(bytes, offset + 4);
    offset += 6;
    if (spec.flip_x) {
      x = -x;
    }
    if (x > std::numeric_limits<std::int16_t>::max()) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    shape.normals[vertex * 3] = static_cast<std::int16_t>(x);
    shape.normals[vertex * 3 + 1] = y;
    shape.normals[vertex * 3 + 2] = z;
  }

  auto texcoord_count = std::size_t{};
  if (shape_has_texcoords(spec.part)) {
    if (!has_range(bytes, offset, 2)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    texcoord_count = static_cast<std::size_t>(read_be16(bytes, offset));
    offset += 2;
    if (texcoord_count > 255 || !has_range(bytes, offset, texcoord_count * 4)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    shape.texcoords.resize(texcoord_count * 2);
    for (auto vertex = std::size_t{}; vertex < texcoord_count; ++vertex) {
      shape.texcoords[vertex * 2] = read_be_s16(bytes, offset);
      shape.texcoords[vertex * 2 + 1] = read_be_s16(bytes, offset + 2);
      offset += 4;
    }
  }

  if (!has_range(bytes, offset, 1)) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }
  const auto primitive_count = static_cast<std::size_t>(bytes[offset++]);
  struct Primitive {
    GXPrimitive type = GX_TRIANGLES;
    std::vector<std::uint8_t> indices;
    std::uint8_t vertex_count = 0;
  };
  auto primitives = std::vector<Primitive>{};
  primitives.reserve(primitive_count);
  auto display_list_capacity = std::size_t{};
  const auto indices_per_vertex = shape_has_texcoords(spec.part) ? std::size_t{3} : std::size_t{2};
  for (auto primitive_index = std::size_t{}; primitive_index < primitive_count; ++primitive_index) {
    if (!has_range(bytes, offset, 2)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    Primitive primitive;
    primitive.vertex_count = bytes[offset++];
    const auto primitive_type = bytes[offset++];
    if (!detail::is_supported_shape_primitive(primitive_type)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    primitive.type = static_cast<GXPrimitive>(primitive_type);
    const auto index_byte_count = static_cast<std::size_t>(primitive.vertex_count) * indices_per_vertex;
    if (primitive.vertex_count == 0 || !has_range(bytes, offset, index_byte_count)) {
      error = CharacterModelError::ResourceMalformed;
      return std::nullopt;
    }
    primitive.indices.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                             bytes.begin() + static_cast<std::ptrdiff_t>(offset + index_byte_count));
    offset += index_byte_count;
    for (auto vertex = std::size_t{}; vertex < primitive.vertex_count; ++vertex) {
      const auto base = vertex * indices_per_vertex;
      if (primitive.indices[base] >= position_count || primitive.indices[base + 1] >= normal_count ||
          (indices_per_vertex == 3 && primitive.indices[base + 2] >= texcoord_count)) {
        error = CharacterModelError::ResourceMalformed;
        return std::nullopt;
      }
    }
    display_list_capacity += 3 + index_byte_count;
    primitives.emplace_back(std::move(primitive));
  }
  if (offset != bytes.size()) {
    error = CharacterModelError::ResourceMalformed;
    return std::nullopt;
  }

  display_list_capacity = (display_list_capacity + 31) & ~std::size_t{31};
  shape.display_list.assign(display_list_capacity, 0);
  GXBeginDisplayList(shape.display_list.data(), static_cast<u32>(shape.display_list.size()));
  for (const auto& primitive : primitives) {
    GXBegin(primitive.type, GX_VTXFMT0, primitive.vertex_count);
    for (const auto index : primitive.indices) {
      GXParam1u8(index);
    }
    GXEnd();
  }
  shape.display_list_size = GXEndDisplayList();
  if (shape.display_list_size == 0 || shape.display_list_size > shape.display_list.size()) {
    error = CharacterModelError::DisplayListOverflow;
    return std::nullopt;
  }
  shape.primitive_count = primitive_count;
  return shape;
}

[[nodiscard]] std::unique_ptr<Texture> load_texture(const ResourceArchive& archive, ResourceArchiveId archive_id,
                                                    std::uint16_t file_index, bool force_clamp,
                                                    CharacterModelError& error) {
  const auto file = archive.file(archive_id, file_index);
  if (!file.has_value()) {
    error = CharacterModelError::ResourceMissing;
    return nullptr;
  }
  auto texture = std::make_unique<Texture>();
  texture->resource.assign(file->begin(), file->end());
  const auto bytes = std::span<const std::uint8_t>{texture->resource};
  const auto descriptor = detail::parse_texture_resource(bytes);
  if (!descriptor.has_value()) {
    error = CharacterModelError::ResourceMalformed;
    return nullptr;
  }
  texture->format = descriptor->format;
  texture->width = descriptor->width;
  texture->height = descriptor->height;
  const auto wrap_s = force_clamp ? GX_CLAMP : descriptor->wrap_s;
  const auto wrap_t = force_clamp ? GX_CLAMP : descriptor->wrap_t;
  texture->image = texture->resource.data() + descriptor->image_offset;
  GXInitTexObj(&texture->object, const_cast<std::uint8_t*>(texture->image), texture->width, texture->height,
               texture->format, wrap_s, wrap_t, GX_FALSE);
  if (force_clamp) {
    GXInitTexObjLOD(&texture->object, descriptor->min_filter, descriptor->mag_filter, descriptor->min_lod,
                    descriptor->max_lod, descriptor->lod_bias, descriptor->bias_clamp ? GX_TRUE : GX_FALSE,
                    descriptor->edge_lod ? GX_TRUE : GX_FALSE, descriptor->max_anisotropy);
  } else {
    GXInitTexObjLOD(&texture->object, GX_LINEAR, GX_LINEAR, 0.0F, 0.0F, 0.0F, GX_FALSE, GX_FALSE, GX_ANISO_1);
  }
  return texture;
}

[[nodiscard]] std::unique_ptr<Texture> load_part_texture(const ResourceArchive& archive, PartTexture part,
                                                         std::uint16_t file, CharacterModelError& error) {
  return load_texture(archive, part_texture_archive(part), file, true, error);
}

[[nodiscard]] std::unique_ptr<Texture> load_shape_texture(const ResourceArchive& archive, ShapeTexture part,
                                                          std::uint16_t file, CharacterModelError& error) {
  return load_texture(archive, shape_texture_archive(part), file, false, error);
}

[[nodiscard]] CharacterInfo info_for_expression(const CharacterInfo& base, RFLExpression expression) {
  auto info = base;
  auto clamp_adjust = [](std::uint8_t& value, int adjustment, int maximum) {
    value = static_cast<std::uint8_t>(std::clamp(static_cast<int>(value) + adjustment, 0, maximum));
  };
  switch (expression) {
  case RFLExp_Normal:
    break;
  case RFLExp_Smile: {
    const auto previous_type = info.eye.type;
    info.eye.type = 48;
    clamp_adjust(info.eye.rotate,
                 static_cast<int>(eye_rotation_offsets[previous_type]) -
                     static_cast<int>(eye_rotation_offsets[info.eye.type]),
                 7);
    break;
  }
  case RFLExp_Anger:
    clamp_adjust(info.eyebrow.rotate, 2, 11);
    clamp_adjust(info.eye.rotate, 2, 7);
    info.mouth.type = 10;
    break;
  case RFLExp_Sorrow:
    clamp_adjust(info.eyebrow.rotate, -2, 11);
    clamp_adjust(info.eye.rotate, -2, 7);
    info.mouth.type = 12;
    break;
  case RFLExp_Surprise: {
    info.eyebrow.y = static_cast<std::uint8_t>(info.eyebrow.y - 2);
    const auto previous_type = info.eye.type;
    info.eye.type = 49;
    clamp_adjust(info.eye.rotate,
                 static_cast<int>(eye_rotation_offsets[previous_type]) -
                     static_cast<int>(eye_rotation_offsets[info.eye.type]),
                 7);
    break;
  }
  case RFLExp_Blink: {
    const auto previous_type = info.eye.type;
    info.eye.type = 26;
    clamp_adjust(info.eye.rotate,
                 static_cast<int>(eye_rotation_offsets[previous_type]) -
                     static_cast<int>(eye_rotation_offsets[info.eye.type]),
                 7);
    break;
  }
  case RFLExp_OpenMouth:
    info.mouth.type = 24;
    break;
  default:
    break;
  }
  return info;
}

void set_face_parts(const CharacterInfo& info, FaceParts& face, std::uint16_t resolution) {
  constexpr auto texture_scale_x = 0.88961464F;
  constexpr auto texture_scale_y = 0.9276675F;
  const auto scale_to_dimension = [](std::uint8_t scale) { return 1.0F + 0.4F * scale; };
  const auto rotation_to_angle = [](unsigned rotation) { return (360.0F / 32.0F) * (rotation % 32); };
  const auto unit = [](float value) { return value / 64.0F; };

  const auto eye_x = texture_scale_x * info.eye.x;
  const auto eye_y = 18.451525F + 1.1600001F * texture_scale_y * info.eye.y;
  const auto eye_width = unit(342.0F) * scale_to_dimension(info.eye.scale);
  const auto eye_height = unit(288.0F) * scale_to_dimension(info.eye.scale);
  const auto eye_angle = rotation_to_angle(info.eye.rotate + eye_rotation_offsets[info.eye.type]);

  const auto eyebrow_x = texture_scale_x * info.eyebrow.x;
  const auto eyebrow_y = 16.549807F + 1.1600001F * texture_scale_y * info.eyebrow.y;
  const auto eyebrow_width = unit(324.0F) * scale_to_dimension(info.eyebrow.scale);
  const auto eyebrow_height = unit(288.0F) * scale_to_dimension(info.eyebrow.scale);
  const auto eyebrow_angle = rotation_to_angle(info.eyebrow.rotate + eyebrow_rotation_offsets[info.eyebrow.type]);

  const auto mouth_y = 29.25885F + 1.1600001F * texture_scale_y * info.mouth.y;
  const auto mouth_width = unit(396.0F) * scale_to_dimension(info.mouth.scale);
  const auto mouth_height = unit(288.0F) * scale_to_dimension(info.mouth.scale);

  const auto mustache_y = 31.763554F + 1.1600001F * texture_scale_y * info.beard.y;
  const auto mustache_width = unit(288.0F) * scale_to_dimension(info.beard.scale);
  const auto mustache_height = unit(576.0F) * scale_to_dimension(info.beard.scale);

  const auto mole_x = 17.766165F + 2.0F * texture_scale_x * info.mole.x;
  const auto mole_y = 17.95986F + 1.1600001F * texture_scale_y * info.mole.y;
  const auto mole_width = scale_to_dimension(info.mole.scale);
  const auto mole_height = scale_to_dimension(info.mole.scale);
  const auto resolution_scale = unit(static_cast<float>(resolution));

  face.right_eye = FacePart{resolution_scale * (32.0F - eye_x),
                            eye_y * resolution_scale,
                            eye_width * resolution_scale,
                            eye_height * resolution_scale,
                            eye_angle,
                            QuadOrigin::Right,
                            face.right_eye.texture};
  face.left_eye = FacePart{resolution_scale * (32.0F + eye_x),
                           eye_y * resolution_scale,
                           eye_width * resolution_scale,
                           eye_height * resolution_scale,
                           360.0F - eye_angle,
                           QuadOrigin::Left,
                           face.left_eye.texture};
  face.right_eyebrow = FacePart{resolution_scale * (32.0F - eyebrow_x),
                                eyebrow_y * resolution_scale,
                                eyebrow_width * resolution_scale,
                                eyebrow_height * resolution_scale,
                                eyebrow_angle,
                                QuadOrigin::Right,
                                face.right_eyebrow.texture};
  face.left_eyebrow = FacePart{resolution_scale * (32.0F + eyebrow_x),
                               eyebrow_y * resolution_scale,
                               eyebrow_width * resolution_scale,
                               eyebrow_height * resolution_scale,
                               360.0F - eyebrow_angle,
                               QuadOrigin::Left,
                               face.left_eyebrow.texture};
  face.mouth = FacePart{32.0F * resolution_scale,
                        mouth_y * resolution_scale,
                        mouth_width * resolution_scale,
                        mouth_height * resolution_scale,
                        0.0F,
                        QuadOrigin::Center,
                        face.mouth.texture};
  face.right_mustache = FacePart{32.0F * resolution_scale,
                                 mustache_y * resolution_scale,
                                 mustache_width * resolution_scale,
                                 mustache_height * resolution_scale,
                                 0.0F,
                                 QuadOrigin::Right,
                                 face.right_mustache.texture};
  face.left_mustache = FacePart{32.0F * resolution_scale,
                                mustache_y * resolution_scale,
                                mustache_width * resolution_scale,
                                mustache_height * resolution_scale,
                                0.0F,
                                QuadOrigin::Left,
                                face.left_mustache.texture};
  face.mole = FacePart{mole_x * resolution_scale,
                       mole_y * resolution_scale,
                       mole_width * resolution_scale,
                       mole_height * resolution_scale,
                       0.0F,
                       QuadOrigin::Center,
                       face.mole.texture};
}

void setup_2d_camera() {
  Mtx44 projection;
  C_MTXOrtho(projection, 0.0F, 448.0F, 0.0F, 608.0F, 0.0F, 1.0F);
  GXSetProjection(projection, GX_ORTHOGRAPHIC);
  GXSetViewport(0.0F, 0.0F, 608.0F, 448.0F, 0.0F, 1.0F);
  GXSetZScaleOffset(1.0F, 0.0F);
  GXSetCullMode(GX_CULL_BACK);
  GXSetZMode(GX_FALSE, GX_LEQUAL, GX_FALSE);
  GXSetZCompLoc(GX_FALSE);
  GXSetAlphaCompare(GX_GREATER, 0, GX_AOP_OR, GX_NEVER, 0);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetDither(GX_FALSE);
  GXClearVtxDesc();
  GXInvalidateVtxCache();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 8);
  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(1);
  GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
  GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_RED, GX_CH_RED, GX_CH_RED, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP2, GX_CH_GREEN, GX_CH_GREEN, GX_CH_GREEN, GX_CH_ALPHA);
  GXSetTevSwapModeTable(GX_TEV_SWAP3, GX_CH_BLUE, GX_CH_BLUE, GX_CH_BLUE, GX_CH_ALPHA);
}

void set_tev_mole() {
  GXSetNumTevStages(1);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevColor(GX_TEVREG0, GXColor{18, 15, 15, 255});
}

void set_tev_mouth(std::uint8_t color) {
  GXSetNumTevStages(3);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_TEXC, GX_CC_ZERO);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP3, GX_TEV_SWAP2);
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C1, GX_CC_TEXC, GX_CC_CPREV);
  GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP2, GX_TEV_SWAP3);
  GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_CPREV);
  GXSetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevColor(GX_TEVREG0, mouth_colors0[color]);
  GXSetTevColor(GX_TEVREG1, mouth_colors1[color]);
}

void set_tev_eye(std::uint8_t color, std::uint8_t type) {
  GXSetNumTevStages(3);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP1, GX_TEV_SWAP1);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_C0, GX_CC_TEXC, GX_CC_ZERO);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevSwapMode(GX_TEVSTAGE1, GX_TEV_SWAP3, GX_TEV_SWAP3);
  GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_C1, GX_CC_TEXC, GX_CC_CPREV);
  GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevSwapMode(GX_TEVSTAGE2, GX_TEV_SWAP2, GX_TEV_SWAP2);
  GXSetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_ONE, GX_CC_TEXC, GX_CC_CPREV);
  GXSetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GXSetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

  auto color0 = GXColor{0, 0, 0, 255};
  if (type == 9) {
    color0 = GXColor{255, 130, 0, 255};
  } else if (type == 20) {
    color0 = GXColor{0, 255, 255, 255};
  }
  GXSetTevColor(GX_TEVREG0, color0);
  GXSetTevColor(GX_TEVREG1, eye_colors[color]);
}

void set_tev_eyebrow(std::uint8_t color) {
  GXSetNumTevStages(1);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevColor(GX_TEVREG0, eyebrow_colors[color]);
}

void set_tev_mustache(std::uint8_t color) {
  GXSetNumTevStages(1);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevColor(GX_TEVREG0, beard_colors[color]);
}

void draw_quad(float x, float y, float width, float height, float rotation_z, QuadOrigin origin) {
  Mtx rotation;
  Mtx position;
  PSMTXIdentity(position);
  PSMTXScaleApply(position, position, width, height, 1.0F);
  PSMTXRotRad(rotation, 'z', (pi / 180.0F) * rotation_z);
  PSMTXConcat(rotation, position, position);
  PSMTXScaleApply(position, position, 0.88961464F, 0.9276675F, 1.0F);
  PSMTXTransApply(position, position, x, y, 0.0F);

  GXLoadPosMtxImm(position, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);

  auto base_x = -0.5F;
  auto s0 = std::int16_t{256};
  auto s1 = std::int16_t{0};
  if (origin == QuadOrigin::Right) {
    base_x = -1.0F;
  } else if (origin == QuadOrigin::Left) {
    base_x = 0.0F;
    s0 = 0;
    s1 = 256;
  }

  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition2f32(1.0F + base_x, -0.5F);
  GXColor1u32(0x000000FF);
  GXTexCoord2s16(s0, 0);
  GXPosition2f32(1.0F + base_x, 0.5F);
  GXColor1u32(0x000000FF);
  GXTexCoord2s16(s0, 256);
  GXPosition2f32(base_x, 0.5F);
  GXColor1u32(0x000000FF);
  GXTexCoord2s16(s1, 256);
  GXPosition2f32(base_x, -0.5F);
  GXColor1u32(0x000000FF);
  GXTexCoord2s16(s1, 0);
  GXEnd();
}

void draw_face_part(const FacePart& part) {
  GXLoadTexObj(&part.texture->object, GX_TEXMAP0);
  draw_quad(part.x, part.y, part.width, part.height, part.angle, part.origin);
}

void draw_all_face_parts(const CharacterInfo& info, const FaceParts& face) {
  set_tev_mustache(info.beard.color);
  draw_face_part(face.right_mustache);
  draw_face_part(face.left_mustache);
  set_tev_mouth(info.mouth.color);
  draw_face_part(face.mouth);
  set_tev_eyebrow(info.eyebrow.color);
  draw_face_part(face.right_eyebrow);
  draw_face_part(face.left_eyebrow);
  set_tev_eye(info.eye.color, info.eye.type);
  draw_face_part(face.right_eye);
  draw_face_part(face.left_eye);
  set_tev_mole();
  draw_face_part(face.mole);
}

void setup_copy_texture(std::uint16_t resolution, void* buffer) {
  GXSetFog(GX_FOG_NONE, 1.0F, 1.0F, 0.0F, 0.0F, GXColor{0, 0, 0, 0});
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);
  GXSetCopyFilter(GX_FALSE, nullptr, GX_FALSE, nullptr);
  GXSetCopyClamp(static_cast<GXFBClamp>(GX_CLAMP_TOP | GX_CLAMP_BOTTOM));
  GXSetTexCopySrc(0, 0, resolution, resolution);
  GXSetTexCopyDst(resolution, resolution, GX_TF_RGB5A3, GX_FALSE);
  GXSetCopyClear(GXColor{0, 0, 0, 0}, 0x00FFFFFF);
  GXCopyTex(buffer, GX_TRUE);
  GXPixModeSync();
}

void capture_mask(void* buffer, const CharacterInfo& info, const FaceParts& face, std::uint16_t resolution) {
  float viewport[6]{};
  GXInvalidateTexAll();
  GXGetViewportv(viewport);
  setup_2d_camera();
  GXSetTevDirect(GX_TEVSTAGE0);
  GXSetTevDirect(GX_TEVSTAGE1);
  GXSetTevDirect(GX_TEVSTAGE2);
  GXSetBlendMode(GX_BM_BLEND, GX_BL_INVDSTALPHA, GX_BL_DSTALPHA, GX_LO_SET);
  GXSetColorUpdate(GX_TRUE);
  draw_all_face_parts(info, face);

  GXSetColorUpdate(GX_FALSE);
  GXCopyTex(buffer, GX_TRUE);
  GXPixModeSync();
  GXSetBlendMode(GX_BM_BLEND, GX_BL_INVDSTALPHA, GX_BL_ONE, GX_LO_SET);
  draw_all_face_parts(info, face);
  GXSetColorUpdate(GX_TRUE);
  GXDrawDone();
  GXCopyTex(buffer, GX_TRUE);
  GXPixModeSync();
  GXSetViewport(viewport[0], viewport[1], viewport[2], viewport[3], viewport[4], viewport[5]);
  (void)resolution;
}

struct ExpressionMask {
  std::vector<std::uint8_t> image;
  GXTexObj object{};
  std::array<std::unique_ptr<Texture>, 5> part_textures;
  bool copied = false;

  ExpressionMask() = default;
  ExpressionMask(const ExpressionMask&) = delete;
  ExpressionMask& operator=(const ExpressionMask&) = delete;
  ~ExpressionMask() {
    GXDestroyTexObj(&object);
    if (!image.empty()) {
      GXDestroyCopyTex(image.data());
    }
  }
};

class ScissorGuard final {
public:
  ScissorGuard() { GXGetScissor(&m_left, &m_top, &m_width, &m_height); }
  ScissorGuard(const ScissorGuard&) = delete;
  ScissorGuard& operator=(const ScissorGuard&) = delete;
  ~ScissorGuard() { GXSetScissor(m_left, m_top, m_width, m_height); }

private:
  std::uint32_t m_left = 0;
  std::uint32_t m_top = 0;
  std::uint32_t m_width = 0;
  std::uint32_t m_height = 0;
};

[[nodiscard]] bool valid_character_info(const CharacterInfo& info) {
  return info.faceline.color < faceline_colors.size() && info.hair.color < hair_colors.size() &&
         info.eye.type < eye_rotation_offsets.size() && info.eye.color < eye_colors.size() &&
         info.eyebrow.type < eyebrow_rotation_offsets.size() && info.eyebrow.color < eyebrow_colors.size() &&
         info.mouth.color < mouth_colors0.size() && info.beard.color < beard_colors.size() &&
         info.glass.color < glass_colors.size() && info.favorite_color < favorite_colors.size();
}

void load_shape_arrays(const Shape& shape, bool with_texcoords) {
  GXSetArray(GX_VA_POS, shape.positions.data(), static_cast<u32>(shape.positions.size() * sizeof(std::int16_t)), 6,
             true);
  GXSetArray(GX_VA_NRM, shape.normals.data(), static_cast<u32>(shape.normals.size() * sizeof(std::int16_t)), 6, true);
  if (with_texcoords) {
    GXSetArray(GX_VA_TEX0, shape.texcoords.data(), static_cast<u32>(shape.texcoords.size() * sizeof(std::int16_t)), 4,
               true);
  }
}

void draw_shape(const Shape& shape, bool with_texcoords) {
  if (shape.empty()) {
    return;
  }
  load_shape_arrays(shape, with_texcoords);
  GXCallDisplayList(shape.display_list.data(), shape.display_list_size);
}

} // namespace

struct CharacterModel::Impl {
  CharacterInfo info;
  std::array<Shape, shape_count> shapes;
  std::array<std::unique_ptr<Texture>, 4> shape_textures;
  std::array<std::unique_ptr<ExpressionMask>, expression_count> masks;
  RFLExpression current_expression = RFLExp_Normal;
  CharacterModelStats statistics;
  Mtx position_matrix{};
  Mtx normal_matrix{};
};

CharacterModel::CharacterModel(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}

CharacterModel::~CharacterModel() = default;

std::unique_ptr<CharacterModel> CharacterModel::create_default(const ResourceArchive& archive, std::uint16_t index,
                                                               RFLResolution resolution, std::uint32_t expression_flags,
                                                               CharacterModelError& error) {
  error = CharacterModelError::None;
  if (!archive.valid()) {
    error = CharacterModelError::ResourceMalformed;
    return nullptr;
  }
  if (index >= default_character_data.size()) {
    error = CharacterModelError::InvalidDefaultIndex;
    return nullptr;
  }
  if (resolution != RFLResolution_64 && resolution != RFLResolution_128 && resolution != RFLResolution_256) {
    error = CharacterModelError::InvalidResolution;
    return nullptr;
  }
  if (expression_flags == 0 || (expression_flags & ~expression_mask) != 0) {
    error = CharacterModelError::InvalidExpressionFlags;
    return nullptr;
  }
  if (AuroraIsFrameActive() != GX_TRUE) {
    error = CharacterModelError::GpuFrameInactive;
    return nullptr;
  }

  auto impl = std::make_unique<Impl>();
  impl->info = decode_default_character(default_character_data[index]);
  if (!valid_character_info(impl->info)) {
    error = CharacterModelError::ResourceMalformed;
    return nullptr;
  }
  PSMTXIdentity(impl->position_matrix);
  PSMTXIdentity(impl->normal_matrix);

  auto nose_translation = Vec3{};
  auto beard_translation = Vec3{};
  auto hair_translation = Vec3{};
  const auto add_shape = [&](ShapeBuildSpec spec) {
    auto shape = build_shape(archive, spec, error);
    if (!shape.has_value()) {
      return false;
    }
    impl->shapes[shape_index(spec.part)] = std::move(*shape);
    return true;
  };

  if (!add_shape(ShapeBuildSpec{.part = ShapePart::Faceline,
                                .file = impl->info.faceline.type,
                                .nose_translation = &nose_translation,
                                .beard_translation = &beard_translation,
                                .hair_translation = &hair_translation}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Cap,
                                .file = impl->info.hair.type,
                                .transform = true,
                                .flip_x = impl->info.hair.flip,
                                .translation = hair_translation}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Hair,
                                .file = impl->info.hair.type,
                                .transform = true,
                                .flip_x = impl->info.hair.flip,
                                .translation = hair_translation}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Forehead,
                                .file = impl->info.hair.type,
                                .transform = true,
                                .flip_x = impl->info.hair.flip,
                                .translation = hair_translation}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Beard,
                                .file = impl->info.beard.type,
                                .transform = true,
                                .translation = beard_translation})) {
    return nullptr;
  }

  const auto nose_scale = 0.4F + 0.175F * impl->info.nose.scale;
  const auto nose_position = Vec3{
      nose_translation.x, nose_translation.y - 1.5F * (static_cast<int>(impl->info.nose.y) - 8), nose_translation.z};
  const auto glass_scale = 0.15F * impl->info.glass.scale + 0.4F;
  const auto glass_position =
      Vec3{nose_translation.x, 5.0F + nose_translation.y - 1.5F * (static_cast<int>(impl->info.glass.y) - 11),
           2.0F + nose_translation.z};
  if (!add_shape(ShapeBuildSpec{.part = ShapePart::Nose,
                                .file = impl->info.nose.type,
                                .transform = true,
                                .scale = nose_scale,
                                .translation = nose_position}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::NoseLine,
                                .file = impl->info.nose.type,
                                .transform = true,
                                .scale = nose_scale,
                                .translation = nose_position}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Mask, .file = impl->info.faceline.type}) ||
      !add_shape(ShapeBuildSpec{.part = ShapePart::Glass,
                                .file = 0,
                                .transform = true,
                                .scale = glass_scale,
                                .translation = glass_position})) {
    return nullptr;
  }

  impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Face)] =
      load_shape_texture(archive, ShapeTexture::Face, impl->info.faceline.texture, error);
  if (!impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Face)]) {
    return nullptr;
  }
  if (!impl->shapes[shape_index(ShapePart::Cap)].empty()) {
    impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Cap)] =
        load_shape_texture(archive, ShapeTexture::Cap, impl->info.hair.type, error);
    if (!impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Cap)]) {
      return nullptr;
    }
  }
  if (!impl->shapes[shape_index(ShapePart::NoseLine)].empty()) {
    impl->shape_textures[static_cast<std::size_t>(ShapeTexture::NoseLine)] =
        load_shape_texture(archive, ShapeTexture::NoseLine, impl->info.nose.type, error);
    if (!impl->shape_textures[static_cast<std::size_t>(ShapeTexture::NoseLine)]) {
      return nullptr;
    }
  }
  impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Glass)] =
      load_shape_texture(archive, ShapeTexture::Glass, impl->info.glass.type, error);
  if (!impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Glass)]) {
    return nullptr;
  }

  const auto mask_resolution = static_cast<std::uint16_t>(resolution);
  const auto mask_size = std::size_t{2} * mask_resolution * mask_resolution;
  auto first_expression = RFLExp_Max;
  const ScissorGuard scissor_guard;
  for (auto expression_index = std::size_t{}; expression_index < expression_count; ++expression_index) {
    if ((expression_flags & (std::uint32_t{1} << expression_index)) == 0) {
      continue;
    }
    const auto expression = static_cast<RFLExpression>(expression_index);
    auto mask = std::make_unique<ExpressionMask>();
    const auto expression_info = info_for_expression(impl->info, expression);
    mask->part_textures[static_cast<std::size_t>(PartTexture::Eye)] =
        load_part_texture(archive, PartTexture::Eye, expression_info.eye.type, error);
    mask->part_textures[static_cast<std::size_t>(PartTexture::Eyebrow)] =
        load_part_texture(archive, PartTexture::Eyebrow, expression_info.eyebrow.type, error);
    mask->part_textures[static_cast<std::size_t>(PartTexture::Mouth)] =
        load_part_texture(archive, PartTexture::Mouth, expression_info.mouth.type, error);
    mask->part_textures[static_cast<std::size_t>(PartTexture::Mustache)] =
        load_part_texture(archive, PartTexture::Mustache, expression_info.beard.mustache, error);
    mask->part_textures[static_cast<std::size_t>(PartTexture::Mole)] =
        load_part_texture(archive, PartTexture::Mole, expression_info.mole.type, error);
    if (std::any_of(mask->part_textures.begin(), mask->part_textures.end(),
                    [](const auto& texture) { return texture == nullptr; })) {
      return nullptr;
    }

    auto face = FaceParts{};
    face.right_eye.texture = mask->part_textures[static_cast<std::size_t>(PartTexture::Eye)].get();
    face.left_eye.texture = face.right_eye.texture;
    face.right_eyebrow.texture = mask->part_textures[static_cast<std::size_t>(PartTexture::Eyebrow)].get();
    face.left_eyebrow.texture = face.right_eyebrow.texture;
    face.mouth.texture = mask->part_textures[static_cast<std::size_t>(PartTexture::Mouth)].get();
    face.right_mustache.texture = mask->part_textures[static_cast<std::size_t>(PartTexture::Mustache)].get();
    face.left_mustache.texture = face.right_mustache.texture;
    face.mole.texture = mask->part_textures[static_cast<std::size_t>(PartTexture::Mole)].get();
    set_face_parts(expression_info, face, mask_resolution);

    mask->image.resize(mask_size);
    setup_copy_texture(mask_resolution, mask->image.data());
    GXSetTexCopySrc(0, 0, mask_resolution, mask_resolution);
    GXSetTexCopyDst(mask_resolution, mask_resolution, GX_TF_RGB5A3, GX_FALSE);
    GXSetScissor(0, 0, mask_resolution, mask_resolution);
    capture_mask(mask->image.data(), expression_info, face, mask_resolution);
    mask->copied = AuroraHasTextureCopy(mask->image.data()) == GX_TRUE;
    if (!mask->copied) {
      error = CharacterModelError::GpuCopyFailed;
      return nullptr;
    }
    GXInitTexObj(&mask->object, mask->image.data(), mask_resolution, mask_resolution, GX_TF_RGB5A3, GX_CLAMP, GX_CLAMP,
                 GX_FALSE);
    GXInitTexObjLOD(&mask->object, GX_LINEAR, GX_LINEAR, 0.0F, 0.0F, 0.0F, GX_FALSE, GX_FALSE, GX_ANISO_1);
    impl->masks[expression_index] = std::move(mask);
    if (first_expression == RFLExp_Max) {
      first_expression = expression;
    }
  }
  impl->current_expression = first_expression;

  for (auto part = std::size_t{}; part < impl->shapes.size(); ++part) {
    impl->statistics.shape_vertex_counts[part] = impl->shapes[part].positions.size() / 3;
    impl->statistics.shape_primitive_counts[part] = impl->shapes[part].primitive_count;
    impl->statistics.display_list_bytes += impl->shapes[part].display_list_size;
  }
  impl->statistics.expression_count = static_cast<std::size_t>(std::popcount(expression_flags));
  impl->statistics.mask_texture_bytes = impl->statistics.expression_count * mask_size;
  return std::unique_ptr<CharacterModel>(new CharacterModel(std::move(impl)));
}

void CharacterModel::set_matrix(const Mtx matrix) {
  PSMTXCopy(matrix, m_impl->position_matrix);
  PSMTXInvXpose(matrix, m_impl->normal_matrix);
}

bool CharacterModel::set_expression(RFLExpression expression) {
  const auto index = static_cast<std::size_t>(expression);
  if (index >= m_impl->masks.size() || !m_impl->masks[index]) {
    return false;
  }
  m_impl->current_expression = expression;
  return true;
}

RFLExpression CharacterModel::expression() const { return m_impl->current_expression; }

bool CharacterModel::mask_ready(RFLExpression expression) const {
  const auto index = static_cast<std::size_t>(expression);
  return index < m_impl->masks.size() && m_impl->masks[index] && m_impl->masks[index]->copied &&
         AuroraHasTextureCopy(m_impl->masks[index]->image.data()) == GX_TRUE;
}

const CharacterModelStats& CharacterModel::stats() const { return m_impl->statistics; }

void load_vertex_setting(const RFLDrawCoreSetting& setting) {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
  GXSetVtxDesc(GX_VA_NRM, GX_INDEX8);
  GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_S16, 8);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_NRM, GX_NRM_XYZ, GX_S16, 14);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_S16, 13);
  GXSetNumTexGens(setting.txcGenNum);
}

void load_material_setting(const RFLDrawCoreSetting& setting) {
  const auto swap_table = static_cast<GXTevSwapSel>(setting.tevSwapTable);
  GXSetTevSwapModeTable(swap_table, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXSetTevSwapModeTable(static_cast<GXTevSwapSel>(setting.tevSwapTable + 1), GX_CH_RED, GX_CH_ALPHA, GX_CH_BLUE,
                        GX_CH_GREEN);
  GXSetNumTevStages(setting.tevStageNum);
  GXSetTevDirect(GX_TEVSTAGE0);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                  static_cast<GXTevRegID>(setting.tevOutRegID));
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE,
                  static_cast<GXTevRegID>(setting.tevOutRegID));
  GXSetTevKColorSel(GX_TEVSTAGE0,
                    static_cast<GXTevKColorSel>(static_cast<unsigned>(GX_TEV_KCSEL_K0) + setting.tevKColorID));
  GXSetTevKAlphaSel(GX_TEVSTAGE0, GX_TEV_KASEL_8_8);
}

void CharacterModel::draw_opaque(const RFLDrawCoreSetting& setting) const {
  const auto texture_coordinate = static_cast<GXTexCoordID>(setting.txcID);
  const auto swap_table = static_cast<GXTevSwapSel>(setting.tevSwapTable);
  const auto output_register = static_cast<GXTevRegID>(setting.tevOutRegID);
  const auto color_register = static_cast<GXTevKColorID>(setting.tevKColorID);
  const auto normal_culling = setting.reverseCulling != 0 ? GX_CULL_FRONT : GX_CULL_BACK;
  const auto reversed_culling = setting.reverseCulling != 0 ? GX_CULL_BACK : GX_CULL_FRONT;

  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST);
  GXSetTevSwapMode(GX_TEVSTAGE0, swap_table, swap_table);
  GXSetTevSwapMode(GX_TEVSTAGE0, swap_table, swap_table);
  GXSetCullMode(normal_culling);
  GXLoadPosMtxImm(m_impl->position_matrix, setting.posNrmMtxID);
  GXLoadNrmMtxImm(m_impl->normal_matrix, setting.posNrmMtxID);
  GXSetCurrentMtx(setting.posNrmMtxID);
  GXSetTexCoordGen2(texture_coordinate, GX_TG_MTX2x4, GX_TG_POS, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
  GXSetVtxDesc(GX_VA_TEX0, GX_NONE);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR_NULL);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);

  const auto& beard = m_impl->shapes[shape_index(ShapePart::Beard)];
  if (!beard.empty()) {
    GXSetTevKColor(color_register, beard_colors[m_impl->info.beard.color]);
    draw_shape(beard, false);
  }

  GXSetTevKColor(color_register, faceline_colors[m_impl->info.faceline.color]);
  draw_shape(m_impl->shapes[shape_index(ShapePart::Nose)], false);

  if (m_impl->info.hair.flip) {
    GXSetCullMode(reversed_culling);
  }
  draw_shape(m_impl->shapes[shape_index(ShapePart::Forehead)], false);
  const auto& hair = m_impl->shapes[shape_index(ShapePart::Hair)];
  if (!hair.empty()) {
    GXSetTevKColor(color_register, hair_colors[m_impl->info.hair.color]);
    draw_shape(hair, false);
  }

  GXSetTevOrder(GX_TEVSTAGE0, texture_coordinate, setting.texMapID, GX_COLOR_NULL);
  GXSetTexCoordGen2(texture_coordinate, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
  GXSetVtxDesc(GX_VA_TEX0, GX_INDEX8);

  const auto& cap = m_impl->shapes[shape_index(ShapePart::Cap)];
  if (!cap.empty()) {
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_KONST);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_DIVIDE_2, GX_TRUE, output_register);
    GXSetTevKColor(color_register, favorite_colors[m_impl->info.favorite_color]);
    GXLoadTexObj(&m_impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Cap)]->object, setting.texMapID);
    draw_shape(cap, true);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, output_register);
  }

  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_KONST, GX_CC_TEXC, GX_CC_TEXA, GX_CC_ZERO);
  GXSetTevKColor(color_register, faceline_colors[m_impl->info.faceline.color]);
  GXSetTevSwapMode(GX_TEVSTAGE0, swap_table, static_cast<GXTevSwapSel>(setting.tevSwapTable + 1));
  if (m_impl->info.hair.flip) {
    GXSetCullMode(normal_culling);
  }
  GXLoadTexObj(&m_impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Face)]->object, setting.texMapID);
  draw_shape(m_impl->shapes[shape_index(ShapePart::Faceline)], true);
}

void CharacterModel::draw_translucent(const RFLDrawCoreSetting& setting) const {
  const auto texture_coordinate = static_cast<GXTexCoordID>(setting.txcID);
  const auto swap_table = static_cast<GXTevSwapSel>(setting.tevSwapTable);
  const auto color_register = static_cast<GXTevKColorID>(setting.tevKColorID);
  const auto normal_culling = setting.reverseCulling != 0 ? GX_CULL_FRONT : GX_CULL_BACK;

  GXSetTevOrder(GX_TEVSTAGE0, texture_coordinate, setting.texMapID, GX_COLOR_NULL);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  GXSetTevSwapMode(GX_TEVSTAGE0, swap_table, swap_table);
  GXLoadPosMtxImm(m_impl->position_matrix, setting.posNrmMtxID);
  GXLoadNrmMtxImm(m_impl->normal_matrix, setting.posNrmMtxID);
  GXSetCurrentMtx(setting.posNrmMtxID);
  GXSetTexCoordGen2(texture_coordinate, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
  GXSetCullMode(normal_culling);

  const auto expression_index = static_cast<std::size_t>(m_impl->current_expression);
  GXLoadTexObj(&m_impl->masks[expression_index]->object, setting.texMapID);
  draw_shape(m_impl->shapes[shape_index(ShapePart::Mask)], true);

  const auto& nose_line = m_impl->shapes[shape_index(ShapePart::NoseLine)];
  if (!nose_line.empty()) {
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO);
    GXLoadTexObj(&m_impl->shape_textures[static_cast<std::size_t>(ShapeTexture::NoseLine)]->object, setting.texMapID);
    draw_shape(nose_line, true);
  }

  const auto& glass = m_impl->shapes[shape_index(ShapePart::Glass)];
  if (!glass.empty()) {
    GXSetTevKColor(color_register, glass_colors[m_impl->info.glass.color]);
    GXSetCullMode(GX_CULL_NONE);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_KONST, GX_CC_TEXC, GX_CC_ZERO);
    GXLoadTexObj(&m_impl->shape_textures[static_cast<std::size_t>(ShapeTexture::Glass)]->object, setting.texMapID);
    draw_shape(glass, true);
  }
}

} // namespace aurora::rfl
