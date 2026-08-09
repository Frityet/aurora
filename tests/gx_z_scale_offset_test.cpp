#include "gx_test_common.hpp"

#include <bit>
#include <cstddef>
#include <cstdint>

namespace {
float read_be_float(const std::vector<u8>& bytes, std::size_t offset) {
  const std::uint32_t bits =
      (static_cast<std::uint32_t>(bytes[offset]) << 24) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
      (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) | static_cast<std::uint32_t>(bytes[offset + 3]);
  return std::bit_cast<float>(bits);
}

void expect_viewport_xf_header(const std::vector<u8>& bytes) {
  ASSERT_EQ(bytes.size(), 29u);
  EXPECT_EQ(bytes[0], 0x10);
  EXPECT_EQ(bytes[1], 0x00);
  EXPECT_EQ(bytes[2], 0x05);
  EXPECT_EQ(bytes[3], 0x10);
  EXPECT_EQ(bytes[4], 0x1A);
}
} // namespace

TEST_F(GXFifoTest, ZScaleOffset_IsDeferredAndAppliedByNextViewport) {
  GXFlush();
  capture_fifo();

  GXSetZScaleOffset(1.0F, 0.0F);
  EXPECT_TRUE(capture_fifo().empty());

  GXSetViewport(0.0F, 0.0F, 608.0F, 448.0F, 0.0F, 1.0F);
  const auto bytes = capture_fifo();
  expect_viewport_xf_header(bytes);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 5), 304.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 9), -224.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 13), 16777216.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 17), 646.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 21), 566.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 25), 16777216.0F);

  GXFlush();
  EXPECT_TRUE(capture_fifo().empty());
}

TEST_F(GXFifoTest, ZScaleOffset_DirtyFlushEncodesConstantFarDepth) {
  GXFlush();
  capture_fifo();

  GXSetViewport(10.0F, 20.0F, 320.0F, 240.0F, 0.0F, 1.0F);
  capture_fifo();

  GXSetZScaleOffset(0.0F, 1.0F);
  EXPECT_TRUE(capture_fifo().empty());

  GXFlush();
  const auto bytes = capture_fifo();
  expect_viewport_xf_header(bytes);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 13), 1.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 25), 16777216.0F);

  reset_gx_state();
  decode_fifo(bytes);
  EXPECT_FLOAT_EQ(gxState().logicalViewport.znear, 16777215.0F / 16777216.0F);
  EXPECT_FLOAT_EQ(gxState().logicalViewport.zfar, 1.0F);

  GXFlush();
  EXPECT_TRUE(capture_fifo().empty());
}

TEST_F(GXFifoTest, ZScaleOffset_MarioDrawRangeRoundTripsWithoutViewportClamping) {
  GXFlush();
  capture_fifo();

  GXSetViewport(0.0F, 0.0F, 640.0F, 456.0F, 0.0F, 1.0F);
  capture_fifo();
  GXSetZScaleOffset(1.0F, 0.00001F);
  GXFlush();
  const auto bytes = capture_fifo();
  expect_viewport_xf_header(bytes);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 13), 16777216.0F);
  EXPECT_FLOAT_EQ(read_be_float(bytes, 25), 16777384.0F);

  reset_gx_state();
  decode_fifo(bytes);
  // The XF viewport offset is a float at roughly 2^24, so Mario's 1e-5
  // request rounds to 168 EFB depth units when it is added to zmax.
  constexpr auto expected_offset = 168.0F / 16777216.0F;
  EXPECT_FLOAT_EQ(gxState().logicalViewport.znear, expected_offset);
  EXPECT_FLOAT_EQ(gxState().logicalViewport.zfar, 1.0F + expected_offset);
}

TEST_F(GXFifoTest, ClipDisableApiIsRejectedExplicitly) {
  EXPECT_DEATH(GXSetClipMode(GX_CLIP_DISABLE), "GX_CLIP_DISABLE");
}

TEST_F(GXFifoTest, ClipDisableDirectXfCommandIsRejectedExplicitly) {
  const std::vector<u8> bytes{
      0x10,                   // GX_LOAD_XF_REG
      0x00, 0x00, 0x10, 0x05, // one value at XF register 0x1005
      0x00, 0x00, 0x00, 0x01, // GX_CLIP_DISABLE
  };
  EXPECT_DEATH(decode_fifo(bytes), "GX_CLIP_DISABLE");
}
