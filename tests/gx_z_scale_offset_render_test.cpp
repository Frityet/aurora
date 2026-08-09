#include <aurora/aurora.h>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace {
constexpr auto CopyWidth = std::uint16_t{256};
constexpr auto CopyHeight = std::uint16_t{192};

enum class DominantColor {
  Red,
  Green,
  Blue,
  Background,
};

void require(bool condition, std::string_view message) {
  if (!condition) {
    throw std::runtime_error(std::string(message));
  }
}

void log_callback(AuroraLogLevel level, const char* module, const char* message, unsigned int length) {
  if (level < LOG_WARNING) {
    return;
  }
  std::cerr << "[aurora:" << (module != nullptr ? module : "unknown") << "] "
            << std::string_view(message != nullptr ? message : "", length) << '\n';
}

class AuroraLifetime final {
public:
  AuroraLifetime() = default;
  AuroraLifetime(const AuroraLifetime&) = delete;
  AuroraLifetime& operator=(const AuroraLifetime&) = delete;
  ~AuroraLifetime() { aurora_shutdown(); }
};

void configure_draw_state() {
  constexpr float projection[4][4] = {
      {1.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, -1.0F, -1.0F},
      {0.0F, 0.0F, 0.0F, 1.0F},
  };
  constexpr float identity[3][4] = {
      {1.0F, 0.0F, 0.0F, 0.0F},
      {0.0F, 1.0F, 0.0F, 0.0F},
      {0.0F, 0.0F, 1.0F, 0.0F},
  };

  GXSetProjection(projection, GX_ORTHOGRAPHIC);
  GXLoadPosMtxImm(identity, GX_PNMTX0);
  GXSetCurrentMtx(GX_PNMTX0);
  GXSetViewport(0.0F, 0.0F, static_cast<float>(CopyWidth), static_cast<float>(CopyHeight), 0.0F, 1.0F);
  GXSetScissor(0, 0, CopyWidth, CopyHeight);

  GXSetCullMode(GX_CULL_NONE);
  GXSetClipMode(GX_CLIP_ENABLE);
  GXSetCoPlanar(GX_DISABLE);
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetZCompLoc(GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetDither(GX_FALSE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  GXSetNumChans(1);
  GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
  GXSetNumTexGens(0);
  GXSetNumIndStages(0);
  GXSetNumTevStages(1);
  GXSetTevDirect(GX_TEVSTAGE0);
  GXSetTevSwapModeTable(GX_TEV_SWAP0, GX_CH_RED, GX_CH_GREEN, GX_CH_BLUE, GX_CH_ALPHA);
  GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
  GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
  GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
}

void draw_quad(float left, float top, float right, float bottom, float z, GXColor color) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(left, top, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(right, top, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(right, bottom, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(left, bottom, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXEnd();
}

[[nodiscard]] std::vector<std::uint8_t> read_display_copy() {
  auto width = 0U;
  auto height = 0U;
  require(AuroraGetDisplayCopySize(&width, &height) == GX_TRUE && width == CopyWidth && height == CopyHeight,
          "GXCopyDisp must materialize the configured display texture");

  auto stride = 0U;
  auto pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4U);
  require(AuroraReadDisplayCopyRGBA8(pixels.data(), static_cast<u32>(pixels.size()), &width, &height, &stride) ==
                  GX_TRUE &&
              stride == width * 4U,
          "the completed Z scale/offset frame must be readable as RGBA8");
  return pixels;
}

[[nodiscard]] bool is_color(std::span<const std::uint8_t, 4> pixel, DominantColor expected) {
  constexpr auto High = std::uint8_t{180};
  constexpr auto Low = std::uint8_t{80};
  switch (expected) {
  case DominantColor::Red:
    return pixel[0] >= High && pixel[1] <= Low && pixel[2] <= Low;
  case DominantColor::Green:
    return pixel[0] <= Low && pixel[1] >= High && pixel[2] <= Low;
  case DominantColor::Blue:
    return pixel[0] <= Low && pixel[1] <= Low && pixel[2] >= High;
  case DominantColor::Background: {
    const auto [low, high] = std::minmax({pixel[0], pixel[1], pixel[2]});
    return high <= 64U && high - low <= 8U;
  }
  }
  return false;
}

[[nodiscard]] bool neighborhood_is(std::span<const std::uint8_t> pixels, std::size_t center_x, std::size_t center_y,
                                   DominantColor expected) {
  constexpr auto Radius = std::size_t{4};
  auto matching = std::size_t{};
  for (auto y = center_y - Radius; y < center_y + Radius; ++y) {
    for (auto x = center_x - Radius; x < center_x + Radius; ++x) {
      const auto offset = (y * CopyWidth + x) * 4U;
      matching += is_color(std::span<const std::uint8_t, 4>{pixels.data() + offset, 4}, expected);
    }
  }
  return matching >= 60U;
}

void prove_exact_mario_z_scale_offset() {
  auto config = AuroraConfig{};
  config.appName = "Aurora GX Z scale/offset render proof";
  config.desiredBackend = BACKEND_VULKAN;
  config.allowCpuAdapter = true;
  config.windowWidth = CopyWidth;
  config.windowHeight = CopyHeight;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logCallback = &log_callback;
  config.logLevel = LOG_WARNING;

  const auto info = aurora_initialize(0, nullptr, &config);
  const auto lifetime = AuroraLifetime{};
  require(info.backend == BACKEND_VULKAN, "the render proof requires Aurora's real Vulkan backend");

  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  require(aurora_begin_frame(), "Aurora must acquire a real render frame");

  GXSetCopyClear(GXColor{24, 24, 24, 255}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, CopyWidth, CopyHeight);
  GXSetDispCopyDst(CopyWidth, CopyHeight);
  GXSetDispCopyYScale(1.0F);
  configure_draw_state();

  constexpr auto Red = GXColor{232, 24, 24, 255};
  constexpr auto Green = GXColor{24, 232, 24, 255};
  constexpr auto Blue = GXColor{24, 24, 232, 255};

  // Reference depth is 0.5. The upper half intentionally starts as solid red.
  GXSetZScaleOffset(1.0F, 0.0F);
  draw_quad(-1.0F, 0.0F, 1.0F, 1.0F, -0.5F, Red);

  // This is the exact state used by MarioActorDraw. It shifts the logical
  // viewport range to [0.00001, 1.00001]. With reversed Z, equal logical
  // depth becomes slightly farther and must fail LEQUAL against the red half.
  GXSetZScaleOffset(1.0F, 0.00001F);
  draw_quad(-1.0F, 0.0F, 0.0F, 1.0F, -0.5F, Green);

  // Moving nearer by more than the offset must still pass depth testing. This
  // also proves the exact-state pipeline really executed, rather than merely
  // leaving the reference image untouched after a validation failure.
  draw_quad(0.0F, 0.0F, 1.0F, 1.0F, -0.4999F, Green);

  // The offset places the far endpoint just outside WebGPU's normal [0, 1]
  // clip volume. DepthClipControl must preserve it so viewport depth clamping
  // produces a valid GX far-plane fragment against the cleared depth value.
  draw_quad(-1.0F, -1.0F, 0.0F, 0.0F, -1.0F, Blue);

  // DepthClipControl also disables WebGPU's fixed depth clipping, so explicit
  // clip distances must retain the original GX volume before the depth-range
  // remap. These fully outside primitives must not replace the valid blue far
  // endpoint or the untouched clear color.
  draw_quad(-1.0F, -1.0F, 0.0F, 0.0F, 0.25F, Red);
  draw_quad(0.0F, -1.0F, 1.0F, 0.0F, -1.25F, Green);

  GXFlush();
  GXCopyDisp(nullptr, GX_TRUE);
  aurora_end_frame();

  const auto pixels = read_display_copy();
  const auto left = CopyWidth / 4U;
  const auto right = CopyWidth * 3U / 4U;
  const auto row_a = CopyHeight / 4U;
  const auto row_b = CopyHeight * 3U / 4U;

  const auto row_a_is_depth_pair = neighborhood_is(pixels, left, row_a, DominantColor::Red) &&
                                   neighborhood_is(pixels, right, row_a, DominantColor::Green);
  const auto row_b_is_depth_pair = neighborhood_is(pixels, left, row_b, DominantColor::Red) &&
                                   neighborhood_is(pixels, right, row_b, DominantColor::Green);
  const auto row_a_is_clip_proof = neighborhood_is(pixels, left, row_a, DominantColor::Blue) &&
                                   neighborhood_is(pixels, right, row_a, DominantColor::Background);
  const auto row_b_is_clip_proof = neighborhood_is(pixels, left, row_b, DominantColor::Blue) &&
                                   neighborhood_is(pixels, right, row_b, DominantColor::Background);

  const auto passed = (row_a_is_depth_pair && row_b_is_clip_proof) || (row_b_is_depth_pair && row_a_is_clip_proof);
  if (!passed) {
    for (const auto [label, x, y] :
         std::array{std::tuple{"left/a", left, row_a}, std::tuple{"right/a", right, row_a},
                    std::tuple{"left/b", left, row_b}, std::tuple{"right/b", right, row_b}}) {
      const auto offset = (y * CopyWidth + x) * 4U;
      std::cerr << label << " rgba=(" << static_cast<unsigned>(pixels[offset]) << ','
                << static_cast<unsigned>(pixels[offset + 1]) << ',' << static_cast<unsigned>(pixels[offset + 2]) << ','
                << static_cast<unsigned>(pixels[offset + 3]) << ")\n";
    }
  }
  require(passed,
          "exact Mario Z scale/offset must preserve equal/near depth behavior, the shifted far endpoint, and original "
          "near/far clipping");
}
} // namespace

int main() {
  try {
    prove_exact_mario_z_scale_offset();
    std::cout << "[ok] exact GXSetZScaleOffset(1, 0.00001) rendered with correct depth behavior\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] exact GXSetZScaleOffset render proof: " << exception.what() << '\n';
    return 1;
  }
}
