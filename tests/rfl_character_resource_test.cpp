#include "rfl/CharacterResource.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace {

void write_be16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 1] = static_cast<std::uint8_t>(value);
}

void write_be32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
  bytes[offset + 1] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 2] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 3] = static_cast<std::uint8_t>(value);
}

std::vector<std::uint8_t> valid_texture(GXTexFmt format = GX_TF_RGB5A3, std::uint16_t width = 4,
                                        std::uint16_t height = 4, std::size_t image_size = 32) {
  auto bytes = std::vector<std::uint8_t>(0x20 + image_size, 0);
  bytes[0] = static_cast<std::uint8_t>(format);
  write_be16(bytes, 2, width);
  write_be16(bytes, 4, height);
  bytes[6] = GX_CLAMP;
  bytes[7] = GX_REPEAT;
  bytes[8] = 0;
  bytes[0x11] = 1;
  bytes[0x12] = 1;
  bytes[0x13] = GX_ANISO_4;
  bytes[0x14] = GX_LIN_MIP_LIN;
  bytes[0x15] = GX_LINEAR;
  bytes[0x16] = 0;
  bytes[0x17] = 10;
  write_be16(bytes, 0x1A, 32);
  write_be32(bytes, 0x1C, 0x20);
  return bytes;
}

TEST(RflCharacterResource, FixedPointConversionRejectsNonFiniteAndOutOfRangeValues) {
  using aurora::rfl::detail::to_fixed_8;
  ASSERT_TRUE(to_fixed_8(1.5F).has_value());
  EXPECT_EQ(*to_fixed_8(1.5F), 384);
  EXPECT_EQ(*to_fixed_8(-1.5F), -384);
  EXPECT_EQ(*to_fixed_8(-8388608.0F), std::numeric_limits<std::int32_t>::min());
  EXPECT_TRUE(to_fixed_8(std::nextafter(8388608.0F, 0.0F)).has_value());
  EXPECT_FALSE(to_fixed_8(8388608.0F).has_value());
  EXPECT_FALSE(to_fixed_8(std::nextafter(-8388608.0F, -std::numeric_limits<float>::infinity())).has_value());
  EXPECT_FALSE(to_fixed_8(std::numeric_limits<float>::infinity()).has_value());
  EXPECT_FALSE(to_fixed_8(-std::numeric_limits<float>::infinity()).has_value());
  EXPECT_FALSE(to_fixed_8(std::numeric_limits<float>::quiet_NaN()).has_value());
}

TEST(RflCharacterResource, ShapePrimitiveAcceptsOnlyRealGXOpcodes) {
  constexpr auto supported = std::array{
      GX_QUADS, GX_TRIANGLES, GX_TRIANGLESTRIP, GX_TRIANGLEFAN, GX_LINES, GX_LINESTRIP, GX_POINTS,
  };
  for (const auto primitive : supported) {
    EXPECT_TRUE(aurora::rfl::detail::is_supported_shape_primitive(static_cast<std::uint8_t>(primitive)));
  }
  EXPECT_FALSE(aurora::rfl::detail::is_supported_shape_primitive(0));
  EXPECT_FALSE(aurora::rfl::detail::is_supported_shape_primitive(0x88));
  EXPECT_FALSE(aurora::rfl::detail::is_supported_shape_primitive(0xFF));
}

TEST(RflCharacterResource, TextureDescriptorDecodesValidatedFieldsAndImageExtent) {
  const auto bytes = valid_texture();
  const auto descriptor = aurora::rfl::detail::parse_texture_resource(bytes);
  ASSERT_TRUE(descriptor.has_value());
  EXPECT_EQ(descriptor->format, GX_TF_RGB5A3);
  EXPECT_EQ(descriptor->width, 4);
  EXPECT_EQ(descriptor->height, 4);
  EXPECT_EQ(descriptor->wrap_s, GX_CLAMP);
  EXPECT_EQ(descriptor->wrap_t, GX_REPEAT);
  EXPECT_TRUE(descriptor->edge_lod);
  EXPECT_TRUE(descriptor->bias_clamp);
  EXPECT_EQ(descriptor->max_anisotropy, GX_ANISO_4);
  EXPECT_EQ(descriptor->min_filter, GX_LIN_MIP_LIN);
  EXPECT_EQ(descriptor->mag_filter, GX_LINEAR);
  EXPECT_EQ(descriptor->image_offset, 0x20u);
  EXPECT_EQ(descriptor->image_size, 32u);
}

TEST(RflCharacterResource, TextureDescriptorCalculatesExactTiledSizeForEverySupportedFormat) {
  struct TextureCase {
    GXTexFmt format;
    std::uint16_t width;
    std::uint16_t height;
    std::size_t image_size;
  };
  constexpr auto cases = std::array{
      TextureCase{GX_TF_I4, 9, 9, 128},    TextureCase{GX_TF_I8, 9, 5, 128},     TextureCase{GX_TF_IA4, 9, 5, 128},
      TextureCase{GX_TF_IA8, 5, 5, 128},   TextureCase{GX_TF_RGB565, 5, 5, 128}, TextureCase{GX_TF_RGB5A3, 5, 5, 128},
      TextureCase{GX_TF_RGBA8, 5, 5, 256}, TextureCase{GX_TF_CMPR, 9, 9, 128},
  };
  for (const auto& test_case : cases) {
    const auto bytes = valid_texture(test_case.format, test_case.width, test_case.height, test_case.image_size);
    const auto descriptor = aurora::rfl::detail::parse_texture_resource(bytes);
    ASSERT_TRUE(descriptor.has_value()) << "format " << static_cast<unsigned>(test_case.format);
    EXPECT_EQ(descriptor->image_size, test_case.image_size);
  }
}

TEST(RflCharacterResource, TextureDescriptorRejectsEveryUnsafeEnumAndBooleanField) {
  struct Mutation {
    std::size_t offset;
    std::uint8_t value;
  };
  constexpr auto mutations = std::array{
      Mutation{0, 0xFF},
      Mutation{6, GX_MAX_TEXWRAPMODE},
      Mutation{7, GX_MAX_TEXWRAPMODE},
      Mutation{8, 1},
      Mutation{0x11, 2},
      Mutation{0x12, 2},
      Mutation{0x13, GX_MAX_ANISOTROPY},
      Mutation{0x14, static_cast<std::uint8_t>(GX_LIN_MIP_LIN + 1)},
      Mutation{0x15, static_cast<std::uint8_t>(GX_LINEAR + 1)},
  };
  for (const auto mutation : mutations) {
    auto bytes = valid_texture();
    bytes[mutation.offset] = mutation.value;
    EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value())
        << "offset 0x" << std::hex << mutation.offset;
  }
}

TEST(RflCharacterResource, TextureDescriptorRejectsInvalidDimensionsOffsetsAndImageSizes) {
  auto bytes = valid_texture();
  write_be16(bytes, 2, 0);
  EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value());

  bytes = valid_texture();
  write_be16(bytes, 4, 1025);
  EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value());

  bytes = valid_texture();
  write_be32(bytes, 0x1C, 0x1F);
  EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value());

  bytes = valid_texture();
  write_be32(bytes, 0x1C, static_cast<std::uint32_t>(bytes.size()));
  EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value());

  bytes = valid_texture();
  bytes.pop_back();
  EXPECT_FALSE(aurora::rfl::detail::parse_texture_resource(bytes).has_value());
}

} // namespace
