#include <aurora/nw4r/brlan.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using aurora::nw4r::lyt::BrlanAnimation;
using aurora::nw4r::lyt::parse_brlan_animation;

constexpr auto kStepCurve = std::uint8_t{1U};
constexpr auto kHermiteCurve = std::uint8_t{2U};

void write_ascii(std::vector<std::uint8_t>& bytes, std::size_t offset, std::string_view value) {
  ASSERT_LE(offset + value.size(), bytes.size());
  for (auto index = std::size_t{}; index < value.size(); ++index) {
    bytes[offset + index] = static_cast<std::uint8_t>(value[index]);
  }
}

void write_be16(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint16_t value) {
  ASSERT_LE(offset + 2U, bytes.size());
  bytes[offset] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value);
}

void write_be32(std::vector<std::uint8_t>& bytes, std::size_t offset, std::uint32_t value) {
  ASSERT_LE(offset + 4U, bytes.size());
  bytes[offset] = static_cast<std::uint8_t>(value >> 24U);
  bytes[offset + 1U] = static_cast<std::uint8_t>(value >> 16U);
  bytes[offset + 2U] = static_cast<std::uint8_t>(value >> 8U);
  bytes[offset + 3U] = static_cast<std::uint8_t>(value);
}

[[nodiscard]] std::vector<std::uint8_t> minimal_brlan() {
  constexpr auto kHeaderSize = std::size_t{0x10U};
  constexpr auto kAnimationBlockSize = std::size_t{0x14U};
  auto bytes = std::vector<std::uint8_t>(kHeaderSize + kAnimationBlockSize, 0U);

  write_ascii(bytes, 0U, "RLAN");
  write_be16(bytes, 4U, 0xFEFFU);
  write_be16(bytes, 6U, 0x0008U);
  write_be32(bytes, 8U, static_cast<std::uint32_t>(bytes.size()));
  write_be16(bytes, 12U, static_cast<std::uint16_t>(kHeaderSize));
  write_be16(bytes, 14U, 1U);

  write_ascii(bytes, kHeaderSize, "pai1");
  write_be32(bytes, kHeaderSize + 4U, static_cast<std::uint32_t>(kAnimationBlockSize));
  write_be16(bytes, kHeaderSize + 8U, 120U);
  bytes[kHeaderSize + 10U] = 1U;
  write_be16(bytes, kHeaderSize + 14U, 0U);
  write_be32(bytes, kHeaderSize + 16U, static_cast<std::uint32_t>(kAnimationBlockSize));
  return bytes;
}

[[nodiscard]] BrlanAnimation::Target step_target(std::uint16_t target,
                                                 std::initializer_list<BrlanAnimation::StepKey> keys) {
  auto value = BrlanAnimation::Target{};
  value.target = target;
  value.curve_type = kStepCurve;
  value.step_keys.assign(keys);
  return value;
}

[[nodiscard]] BrlanAnimation::Target hermite_target(std::uint16_t target,
                                                    std::initializer_list<BrlanAnimation::HermiteKey> keys) {
  auto value = BrlanAnimation::Target{};
  value.target = target;
  value.curve_type = kHermiteCurve;
  value.hermite_keys.assign(keys);
  return value;
}

TEST(BrlanParser, ParsesARealMinimalBigEndianAnimationBlock) {
  const auto animation = parse_brlan_animation(minimal_brlan());

  EXPECT_EQ(animation.frame_size, 120U);
  EXPECT_TRUE(animation.loop);
  EXPECT_TRUE(animation.contents.empty());
}

TEST(BrlanParser, RejectsMissingMagicWrongEndianAndTruncatedBlocks) {
  auto missing_magic = minimal_brlan();
  missing_magic[0U] = 0U;
  EXPECT_THROW((void)parse_brlan_animation(missing_magic), std::runtime_error);

  auto wrong_endian = minimal_brlan();
  write_be16(wrong_endian, 4U, 0xFFFEU);
  EXPECT_THROW((void)parse_brlan_animation(wrong_endian), std::runtime_error);

  auto truncated_block = minimal_brlan();
  write_be32(truncated_block, 0x14U, 0x100U);
  EXPECT_THROW((void)parse_brlan_animation(truncated_block), std::runtime_error);
}

TEST(BrlanEvaluator, SamplesGenericPaneCurvesWithoutSequenceKnowledge) {
  auto animation = BrlanAnimation{};
  auto pane = BrlanAnimation::Content{};
  pane.name = "GenericPane";
  pane.infos = {
      BrlanAnimation::Info{
          .kind = "RLPA",
          .targets =
              {
                  hermite_target(0U, {{0.0F, 0.0F, 1.0F}, {10.0F, 10.0F, 1.0F}}),
                  step_target(1U, {{0.0F, 2U}, {5.0F, 8U}}),
              },
      },
      BrlanAnimation::Info{
          .kind = "RLVC",
          .targets = {step_target(16U, {{0.0F, 200U}})},
      },
      BrlanAnimation::Info{
          .kind = "RLVI",
          .targets = {step_target(0U, {{0.0F, 1U}, {6.0F, 0U}})},
      },
  };
  animation.contents.push_back(std::move(pane));

  const auto early = animation.pane_frame("GenericPane", 4.0F);
  ASSERT_TRUE(early.translate_x.has_value());
  ASSERT_TRUE(early.translate_y.has_value());
  ASSERT_TRUE(early.alpha.has_value());
  ASSERT_TRUE(early.visible.has_value());
  EXPECT_NEAR(*early.translate_x, 4.0F, 0.0001F);
  EXPECT_FLOAT_EQ(*early.translate_y, 2.0F);
  EXPECT_FLOAT_EQ(*early.alpha, 200.0F);
  EXPECT_TRUE(*early.visible);

  const auto late = animation.pane_frame("GenericPane", 7.0F);
  ASSERT_TRUE(late.translate_y.has_value());
  ASSERT_TRUE(late.visible.has_value());
  EXPECT_FLOAT_EQ(*late.translate_y, 8.0F);
  EXPECT_FALSE(*late.visible);

  const auto absent = animation.pane_frame("DifferentPane", 4.0F);
  EXPECT_FALSE(absent.translate_x.has_value());
  EXPECT_FALSE(absent.visible.has_value());
}

TEST(BrlanEvaluator, SamplesGenericTextureAndMaterialTargets) {
  auto animation = BrlanAnimation{};
  animation.contents = {
      BrlanAnimation::Content{
          .name = "GenericMaterial",
          .infos =
              {
                  BrlanAnimation::Info{
                      .kind = "RLTS",
                      .targets =
                          {
                              hermite_target(0U, {{0.0F, 0.0F, 0.1F}, {10.0F, 1.0F, 0.1F}}),
                              step_target(4U, {{0.0F, 2U}}),
                          },
                  },
                  BrlanAnimation::Info{
                      .kind = "RLMC",
                      .targets =
                          {
                              step_target(0U, {{0.0F, 255U}}),
                              step_target(4U, {{0.0F, 64U}}),
                              step_target(16U, {{0.0F, 32U}}),
                          },
                  },
              },
      },
  };

  const auto texture = animation.texture_frame("GenericMaterial", 5.0F);
  ASSERT_TRUE(texture.translate_s.has_value());
  ASSERT_TRUE(texture.scale_t.has_value());
  EXPECT_NEAR(*texture.translate_s, 0.5F, 0.0001F);
  EXPECT_FLOAT_EQ(*texture.scale_t, 2.0F);

  const auto material = animation.material_frame("GenericMaterial", 5.0F);
  ASSERT_TRUE(material.material_color[0U].has_value());
  ASSERT_TRUE(material.tev_colors[0U][0U].has_value());
  ASSERT_TRUE(material.tev_k_colors[0U][0U].has_value());
  EXPECT_FLOAT_EQ(*material.material_color[0U], 255.0F);
  EXPECT_FLOAT_EQ(*material.tev_colors[0U][0U], 64.0F);
  EXPECT_FLOAT_EQ(*material.tev_k_colors[0U][0U], 32.0F);
}

} // namespace
