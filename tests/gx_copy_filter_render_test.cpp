#include <aurora/aurora.h>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr auto Width = std::uint16_t{64};
constexpr auto Height = std::uint16_t{48};
constexpr auto ProbeRow = std::uint16_t{20};

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
  GXSetViewport(0.0F, 0.0F, static_cast<float>(Width), static_cast<float>(Height), 0.0F, 1.0F);
  GXSetScissor(0, 0, Width, Height);

  GXSetCullMode(GX_CULL_NONE);
  GXSetClipMode(GX_CLIP_ENABLE);
  GXSetCoPlanar(GX_DISABLE);
  GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
  GXSetZCompLoc(GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetDither(GX_FALSE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXSetPixelFmt(GX_PF_RGBA6_Z24, GX_ZC_LINEAR);

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

void draw_fullscreen(GXColor color) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(-1.0F, 1.0F, 0.0F);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, 1.0F, 0.0F);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, -1.0F, 0.0F);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(-1.0F, -1.0F, 0.0F);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXEnd();
}

void draw_test_rows() {
  GXSetScissor(0, 0, Width, Height);
  draw_fullscreen(GXColor{0, 0, 0, 252});

  GXSetScissor(0, ProbeRow - 1, Width, 1);
  draw_fullscreen(GXColor{252, 0, 0, 252});
  GXSetScissor(0, ProbeRow, Width, 1);
  draw_fullscreen(GXColor{0, 252, 0, 84});
  GXSetScissor(0, ProbeRow + 1, Width, 1);
  draw_fullscreen(GXColor{0, 0, 252, 0});
  GXSetScissor(0, 0, Width, Height);
}

std::array<std::uint8_t, 4> read_probe() {
  u32 width = 0;
  u32 height = 0;
  u32 stride = 0;
  std::vector<std::uint8_t> pixels(static_cast<std::size_t>(Width) * Height * 4);
  require(AuroraReadDisplayCopyRGBA8(pixels.data(), static_cast<u32>(pixels.size()), &width, &height, &stride) == TRUE,
          "display copy must be readable");
  require(width == Width && height == Height && stride == Width * 4, "display-copy dimensions must remain exact");
  const auto offset = static_cast<std::size_t>(ProbeRow) * stride + static_cast<std::size_t>(Width / 2) * 4;
  return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

bool close_to(std::uint8_t actual, std::uint8_t expected) {
  return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= 2;
}

void require_color(const std::array<std::uint8_t, 4>& actual, const std::array<std::uint8_t, 4>& expected,
                   std::string_view label) {
  if (!std::equal(actual.begin(), actual.end(), expected.begin(), &close_to)) {
    throw std::runtime_error(std::string(label) + ": expected rgba(" + std::to_string(expected[0]) + "," +
                             std::to_string(expected[1]) + "," + std::to_string(expected[2]) + "," +
                             std::to_string(expected[3]) + "), got rgba(" + std::to_string(actual[0]) + "," +
                             std::to_string(actual[1]) + "," + std::to_string(actual[2]) + "," +
                             std::to_string(actual[3]) + ")");
  }
}

void begin_test_frame() {
  aurora_update();
  require(aurora_begin_frame(), "Aurora must acquire a real render frame");
  configure_draw_state();
  draw_test_rows();
}

void copy_test_frame(void* destination) {
  GXSetCopyClear(GXColor{0, 0, 0, 0}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, Width, Height);
  GXSetDispCopyDst(Width, Height);
  GXSetDispCopyYScale(1.0F);
  GXSetCopyClamp(static_cast<GXFBClamp>(GX_CLAMP_TOP | GX_CLAMP_BOTTOM));
  GXCopyDisp(destination, GX_TRUE);
  aurora_end_frame();
}

void prove_copy_filter() {
  AuroraConfig config{};
  config.appName = "Aurora GX copy-filter render proof";
#if defined(__APPLE__)
  config.desiredBackend = BACKEND_METAL;
#else
  config.desiredBackend = BACKEND_VULKAN;
#endif
  config.allowCpuAdapter = true;
  config.windowWidth = Width;
  config.windowHeight = Height;
  config.msaa = 1;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logCallback = &log_callback;
  config.logLevel = LOG_WARNING;

  const auto init = aurora_initialize(0, nullptr, &config);
  const AuroraLifetime lifetime;
  require(init.backend == config.desiredBackend, "the render proof requires the requested real GPU backend");

  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  VISetFrameBufferScale(1.0F);
  GXRenderModeObj render_mode{};
  render_mode.viTVmode = VI_TVMODE_NTSC_PROG;
  render_mode.fbWidth = Width;
  render_mode.efbHeight = Height;
  render_mode.xfbHeight = Height;
  render_mode.viWidth = Width;
  render_mode.viHeight = Height;
  render_mode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&render_mode);

  begin_test_frame();
  constexpr u8 retailSmgFilter[7]{32, 0, 32, 0, 0, 0, 0};
  GXSetCopyFilter(GX_FALSE, nullptr, GX_TRUE, retailSmgFilter);
  copy_test_frame(reinterpret_cast<void*>(std::uintptr_t{0x1000}));
  require_color(read_probe(), {126, 126, 0, 84}, "retail SMG 32/32/0 copy filter");

  begin_test_frame();
  constexpr u8 filter[7]{16, 0, 32, 0, 0, 0, 16};
  GXSetCopyFilter(GX_FALSE, nullptr, GX_TRUE, filter);
  copy_test_frame(reinterpret_cast<void*>(std::uintptr_t{0x2000}));
  require_color(read_probe(), {63, 126, 63, 84}, "enabled 16/32/16 copy filter");

  begin_test_frame();
  GXSetCopyFilter(GX_FALSE, nullptr, GX_FALSE, nullptr);
  copy_test_frame(reinterpret_cast<void*>(std::uintptr_t{0x3000}));
  require_color(read_probe(), {0, 252, 0, 84}, "disabled copy filter identity");
}
} // namespace

int main() {
  try {
    prove_copy_filter();
    std::cout << "[ok] GX display-copy vfilter preserves retail three-row RGB and current-row alpha semantics\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] GX display-copy vfilter proof: " << exception.what() << '\n';
    return 1;
  }
}
