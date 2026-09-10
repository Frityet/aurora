#include <aurora/aurora.h>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {
constexpr auto Width = std::uint16_t{256};
constexpr auto Height = std::uint16_t{192};

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
  GXSetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
  GXSetZCompLoc(GX_FALSE);
  GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_COPY);
  GXSetColorUpdate(GX_TRUE);
  GXSetAlphaUpdate(GX_TRUE);
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetDither(GX_FALSE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXSetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);
  GXSetFog(GX_FOG_NONE, 0.0F, 1.0F, 0.0F, 1.0F, GXColor{});
  GXSetFogRangeAdj(GX_FALSE, 0, nullptr);

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
  GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
}

void draw_fullscreen(float z, GXColor color) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(-1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXTexCoord2f32(0.25F, 0.25F);
  GXPosition3f32(1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXTexCoord2f32(0.25F, 0.25F);
  GXPosition3f32(1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXTexCoord2f32(0.25F, 0.25F);
  GXPosition3f32(-1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXTexCoord2f32(0.25F, 0.25F);
  GXEnd();
}

AuroraDepthSnapshotInfo wait_for_snapshot(AuroraDepthSnapshotId id) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{5};
  AuroraDepthSnapshotInfo info{};
  while (std::chrono::steady_clock::now() < deadline) {
    const auto status = GXAuroraGetDepthSnapshotInfo(id, &info);
    if (status == AURORA_DEPTH_SNAPSHOT_READY) {
      return info;
    }
    require(status == AURORA_DEPTH_SNAPSHOT_PENDING, "tagged depth snapshot was dropped or expired");
    aurora_update();
    std::this_thread::sleep_for(std::chrono::milliseconds{1});
  }
  throw std::runtime_error("timed out waiting for tagged depth snapshot");
}

std::vector<std::uint8_t> synchronize_display_copy() {
  u32 width = 0;
  u32 height = 0;
  u32 stride = 0;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(Width) * Height * 4);
  require(AuroraReadDisplayCopyRGBA8(pixels.data(), static_cast<u32>(pixels.size()), &width, &height, &stride) == TRUE,
          "display-copy readback must synchronize the tagged snapshot frame");
  require(width == Width && height == Height && stride == Width * 4, "display-copy dimensions must remain exact");
  return pixels;
}

struct Texture {
  alignas(32) std::array<u8, 64> bytes{};
  GXTexObj object{};

  void init(GXTexFmt format) {
    if (format == GX_TF_Z8) {
      bytes.fill(0xab);
    } else if (format == GX_TF_Z16) {
      for (unsigned i = 0; i < 32; i += 2) {
        bytes[i] = 0x34; // IA8: alpha carries the high depth byte.
        bytes[i + 1] = 0x12;
      }
    } else {
      for (unsigned i = 0; i < 32; i += 2) {
        bytes[i] = 0x78;
        bytes[i + 1] = 0x12;
        bytes[32 + i] = 0x34;
        bytes[33 + i] = 0x56;
      }
    }
    GXInitTexObj(&object, bytes.data(), format == GX_TF_Z8 ? 8 : 4, 4, format, GX_REPEAT, GX_REPEAT, GX_FALSE);
    GXInitTexObjLOD(&object, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  }
};

void bind_texture(Texture& texture) {
  GXLoadTexObj(&texture.object, GX_TEXMAP0);
  GXSetNumTexGens(1);
  GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
  GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
}

void check_depth(AuroraDepthSnapshotId id, u32 expected, std::string_view label) {
  const auto info = wait_for_snapshot(id);
  require(info.width == Width && info.height == Height, "depth snapshot must retain exact native dimensions");
  for (const auto& point :
       std::array<std::array<u16, 2>, 3>{{{32, 32}, {Width / 2, Height / 2}, {Width - 32, Height - 32}}}) {
    u32 actual = 0;
    require(GXAuroraReadDepthSnapshotZ(id, point[0], point[1], &actual) == TRUE, "depth sample must be available");
    // Depth32Float and the reversed-depth readback can round by one 24-bit unit.
    require(std::abs(static_cast<s32>(actual) - static_cast<s32>(expected)) <= 1,
            std::string(label) + ": expected depth " + std::to_string(expected) + ", observed " +
                std::to_string(actual));
  }
  GXAuroraReleaseDepthSnapshot(id);
}

void begin_frame() {
  aurora_update();
  require(aurora_begin_frame(), "Aurora must acquire a real GPU frame");
  configure_draw_state();
}

void finish_frame() {
  GXCopyDisp(nullptr, GX_TRUE);
  aurora_end_frame();
}

enum class SampleMode { Normal, NoTexture, NoTexGens, LastStageDisabled, LastStageEnabled, LastOfTwoEnabled, Swapped };
struct DepthCase {
  const char* name;
  GXTexFmt sourceFormat;
  GXTexFmt depthFormat;
  GXZTexOp operation;
  u32 bias;
  u32 expected;
  bool early = false;
  SampleMode sample = SampleMode::Normal;
  bool displayList = false;
};

void run_case(const DepthCase& test) {
  begin_frame();
  Texture texture;
  texture.init(test.sourceFormat);
  Texture lastTexture;
  bind_texture(texture);
  if (test.sample == SampleMode::NoTexture) {
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  } else if (test.sample == SampleMode::NoTexGens) {
    GXSetNumTexGens(0);
  } else if (test.sample == SampleMode::LastStageDisabled || test.sample == SampleMode::LastStageEnabled ||
             test.sample == SampleMode::LastOfTwoEnabled) {
    GXSetNumTevStages(2);
    GXSetTevDirect(GX_TEVSTAGE1);
    GXSetTevOrder(GX_TEVSTAGE1, test.sample == SampleMode::LastStageDisabled ? GX_TEXCOORD_NULL : GX_TEXCOORD0,
                  test.sample == SampleMode::LastStageDisabled ? GX_TEXMAP_NULL : GX_TEXMAP0, GX_COLOR0A0);
    if (test.sample == SampleMode::LastStageEnabled) {
      GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    } else if (test.sample == SampleMode::LastOfTwoEnabled) {
      lastTexture.init(GX_TF_RGBA8);
      for (unsigned i = 1; i < 32; i += 2)
        lastTexture.bytes[i] = 0x65;
      GXLoadTexObj(&lastTexture.object, GX_TEXMAP1);
      GXSetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD0, GX_TEXMAP1, GX_COLOR0A0);
    }
    GXSetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GXSetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
    GXSetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  } else if (test.sample == SampleMode::Swapped) {
    GXSetTevSwapModeTable(GX_TEV_SWAP1, GX_CH_BLUE, GX_CH_ALPHA, GX_CH_RED, GX_CH_GREEN);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP1);
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_TEXC);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_TEXA);
  }
  alignas(32) std::array<u8, 32> displayList{};
  if (test.displayList) {
    const u32 type = test.depthFormat == GX_TF_Z8 ? 0 : test.depthFormat == GX_TF_Z16 ? 1 : 2;
    const std::array<u32, 2> commands{0xf4000000u | (test.bias & 0xffffffu),
                                      0xf5000000u | type | (static_cast<u32>(test.operation) << 2)};
    for (unsigned i = 0; i < commands.size(); ++i) {
      displayList[i * 5] = 0x61;
      for (unsigned byte = 0; byte < 4; ++byte)
        displayList[i * 5 + 1 + byte] = commands[i] >> (24 - byte * 8);
    }
    GXCallDisplayList(displayList.data(), displayList.size());
  } else {
    GXSetZTexture(test.operation, test.depthFormat, test.bias);
  }
  GXSetZCompLoc(test.early ? GX_TRUE : GX_FALSE);
  draw_fullscreen(-0.5F, GXColor{232, 24, 24, 255});
  const auto id = GXAuroraRequestDepthSnapshot();
  finish_frame();
  synchronize_display_copy();
  check_depth(id, test.expected, test.name);
  std::cout << "[ok] " << test.name << '\n';
}

void prove_state_boundaries(Texture& texture) {
  begin_frame();
  bind_texture(texture);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0x010000);
  draw_fullscreen(-0.5F, GXColor{232, 24, 24, 255});
  const auto first = GXAuroraRequestDepthSnapshot();
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0x020000);
  draw_fullscreen(-0.5F, GXColor{24, 232, 24, 255});
  const auto second = GXAuroraRequestDepthSnapshot();
  GXSetZTexture(GX_ZT_DISABLE, GX_TF_Z24X8, 0);
  draw_fullscreen(-0.25F, GXColor{24, 24, 232, 255});
  const auto third = GXAuroraRequestDepthSnapshot();
  finish_frame();
  synchronize_display_copy();
  check_depth(first, 0x133456, "same-stream first bias");
  check_depth(second, 0x143456, "same-stream second bias");
  check_depth(third, 0x400000, "same-stream disable restores raster depth");
  std::cout << "[ok] FIFO boundaries retain each draw's bias and depth operation\n";
}

void prove_depth_only_clear() {
  begin_frame();
  draw_fullscreen(-0.25F, GXColor{232, 24, 24, 255});
  Texture farTexture;
  farTexture.init(GX_TF_Z24X8);
  farTexture.bytes.fill(0xff);
  GXInvalidateTexAll();
  bind_texture(farTexture);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0);
  GXSetColorUpdate(GX_FALSE);
  GXSetAlphaUpdate(GX_FALSE);
  draw_fullscreen(-0.5F, GXColor{24, 232, 24, 255});
  const auto cleared = GXAuroraRequestDepthSnapshot();
  GXCopyDisp(nullptr, GX_FALSE);
  aurora_end_frame();
  const auto pixels = synchronize_display_copy();
  const size_t pixel = (Height / 2 * Width + Width / 2) * 4;
  require(pixels[pixel] > 220 && pixels[pixel + 1] < 32, "depth-only clear must preserve the prior EFB color");
  check_depth(cleared, 0xffffff, "textured far-depth clear");

  begin_frame();
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  draw_fullscreen(-0.75F, GXColor{24, 232, 24, 255});
  const auto subsequent = GXAuroraRequestDepthSnapshot();
  finish_frame();
  const auto after = synchronize_display_copy();
  require(after[pixel] < 32 && after[pixel + 1] > 220, "farther geometry must pass the cleared depth test");
  check_depth(subsequent, 0xc00000, "geometry after textured far-depth clear");
  std::cout << "[ok] textured far-depth clear preserves color and allows later farther geometry\n";
}

void prove_late_depth_comparison() {
  begin_frame();
  draw_fullscreen(-0.25F, GXColor{232, 24, 24, 255});
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetNumTexGens(0);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0xc00000);
  draw_fullscreen(-0.125F, GXColor{24, 232, 24, 255});
  const auto rejected = GXAuroraRequestDepthSnapshot();
  finish_frame();
  const auto pixels = synchronize_display_copy();
  const size_t pixel = (Height / 2 * Width + Width / 2) * 4;
  require(pixels[pixel] > 220 && pixels[pixel + 1] < 32,
          "late comparison must reject texture depth even when geometric depth would pass");
  check_depth(rejected, 0x400000, "late depth comparison rejection");

  begin_frame();
  draw_fullscreen(-0.25F, GXColor{232, 24, 24, 255});
  GXSetZMode(GX_TRUE, GX_LEQUAL, GX_TRUE);
  GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0x200000);
  draw_fullscreen(-0.75F, GXColor{24, 232, 24, 255});
  const auto accepted = GXAuroraRequestDepthSnapshot();
  finish_frame();
  const auto after = synchronize_display_copy();
  require(after[pixel] < 32 && after[pixel + 1] > 220,
          "late comparison must accept texture depth even when geometric depth would fail");
  check_depth(accepted, 0x200000, "late depth comparison acceptance");
  std::cout << "[ok] late comparison uses textured depth for both rejection and acceptance\n";
}

void prove_fog_uses_texture_depth() {
  for (const bool early : {false, true}) {
    begin_frame();
    GXSetFog(GX_FOG_ORTHO_LIN, 0.0F, 1.0F, 0.0F, 1.0F, GXColor{0, 0, 255, 255});
    GXSetZTexture(GX_ZT_REPLACE, GX_TF_Z24X8, 0xffffff);
    GXSetZCompLoc(early ? GX_TRUE : GX_FALSE);
    draw_fullscreen(-0.25F, GXColor{255, 0, 0, 255});
    const auto depth = GXAuroraRequestDepthSnapshot();
    finish_frame();
    const auto pixels = synchronize_display_copy();
    const size_t pixel = (Height / 2 * Width + Width / 2) * 4;
    require(pixels[pixel] < 2 && pixels[pixel + 2] > 253,
            "fog must use the Z texture result for both early and late depth comparison");
    check_depth(depth, early ? 0x400000 : 0xffffff, "fog and depth comparison location");
  }
  std::cout << "[ok] fog uses texture depth with early and late depth comparison\n";
}

void prove_z_texture() {
  AuroraConfig config{};
  config.appName = "Aurora GX Z texture depth render proof";
#if defined(__APPLE__)
  config.desiredBackend = BACKEND_METAL;
#else
  config.desiredBackend = BACKEND_VULKAN;
#endif
  config.allowCpuAdapter = true;
  config.windowWidth = Width;
  config.windowHeight = Height;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logCallback = &log_callback;
  config.logLevel = LOG_WARNING;
  const auto init = aurora_initialize(0, nullptr, &config);
  const AuroraLifetime lifetime;
  require(init.backend == config.desiredBackend, "proof requires the requested real GPU backend");
  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  VISetFrameBufferScale(1.0F);
  GXRenderModeObj mode{};
  mode.viTVmode = VI_TVMODE_NTSC_PROG;
  mode.fbWidth = mode.viWidth = Width;
  mode.efbHeight = mode.xfbHeight = mode.viHeight = Height;
  mode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&mode);
  GXSetCopyClear(GXColor{16, 16, 16, 255}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, Width, Height);
  GXSetDispCopyDst(Width, Height);
  GXSetDispCopyYScale(1.0F);

  const std::array cases{
      DepthCase{"Z8 tiled source", GX_TF_Z8, GX_TF_Z8, GX_ZT_REPLACE, 0, 0xab},
      DepthCase{"Z16 tiled source and alpha high byte", GX_TF_Z16, GX_TF_Z16, GX_ZT_REPLACE, 0, 0x3412},
      DepthCase{"Z24X8 two-plane source", GX_TF_Z24X8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x123456},
      DepthCase{"Z8 selects RGBA alpha", GX_TF_RGBA8, GX_TF_Z8, GX_ZT_REPLACE, 0, 0x78},
      DepthCase{"Z16 selects RGBA red and alpha", GX_TF_RGBA8, GX_TF_Z16, GX_ZT_REPLACE, 0, 0x7812},
      DepthCase{"Z24 selects RGBA RGB", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x123456},
      DepthCase{"replacement bias", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0x234567, 0x3579bd},
      DepthCase{"bias masks to 24 bits", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0x21000000u, 0x123456},
      DepthCase{"replacement 24-bit overflow", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0xf00000, 0x023456},
      DepthCase{"addition includes raster depth", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_ADD, 0, 0x923456},
      DepthCase{"addition 24-bit overflow", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_ADD, 0x800000, 0x123456},
      DepthCase{"early Z comparison retains raster depth", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x800000, true},
      DepthCase{"disabled Z texture retains raster depth", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_DISABLE, 0, 0x800000},
      DepthCase{"no enabled texture samples zero", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0x123456, 0x123456, false,
                SampleMode::NoTexture},
      DepthCase{"no texture generators samples zero", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0x123456, 0x123456,
                false, SampleMode::NoTexGens},
      DepthCase{"later disabled stage retains last raw texture", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x123456,
                false, SampleMode::LastStageDisabled},
      DepthCase{"last enabled stage is sampled despite unused TEV input", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0,
                0x123456, false, SampleMode::LastStageEnabled},
      DepthCase{"last of two enabled textures supplies depth", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x653456,
                false, SampleMode::LastOfTwoEnabled},
      DepthCase{"TEV swap does not swap Z channels", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_REPLACE, 0, 0x123456, false,
                SampleMode::Swapped},
      DepthCase{"raw BP display list has identical state", GX_TF_RGBA8, GX_TF_Z24X8, GX_ZT_ADD, 0x100000, 0xa23456,
                false, SampleMode::Normal, true},
  };
  for (const auto& test : cases)
    run_case(test);
  Texture texture;
  texture.init(GX_TF_RGBA8);
  prove_state_boundaries(texture);
  prove_depth_only_clear();
  prove_late_depth_comparison();
  prove_fog_uses_texture_depth();
}
} // namespace

int main() {
  try {
    prove_z_texture();
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] Z texture render proof: " << exception.what() << '\n';
    return 1;
  }
}
