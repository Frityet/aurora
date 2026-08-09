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

void draw_fullscreen(float z, GXColor color) {
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  GXPosition3f32(-1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, 1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
  GXPosition3f32(-1.0F, -1.0F, z);
  GXColor4u8(color.r, color.g, color.b, color.a);
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

void synchronize_display_copy() {
  u32 width = 0;
  u32 height = 0;
  u32 stride = 0;
  std::vector<std::uint8_t> pixels(static_cast<size_t>(Width) * Height * 4);
  require(AuroraReadDisplayCopyRGBA8(pixels.data(), static_cast<u32>(pixels.size()), &width, &height, &stride) == TRUE,
          "display-copy readback must synchronize the tagged snapshot frame");
  require(width == Width && height == Height && stride == Width * 4, "display-copy dimensions must remain exact");
}

void prove_tagged_depth_boundaries() {
  AuroraConfig config{};
  config.appName = "Aurora tagged depth snapshot render proof";
  config.desiredBackend = BACKEND_VULKAN;
  config.allowCpuAdapter = true;
  config.windowWidth = Width;
  config.windowHeight = Height;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logCallback = &log_callback;
  config.logLevel = LOG_WARNING;

  const auto init = aurora_initialize(0, nullptr, &config);
  const AuroraLifetime lifetime;
  require(init.backend == BACKEND_VULKAN, "the render proof requires Aurora's Vulkan backend");

  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  VISetFrameBufferScale(1.0F);
  GXRenderModeObj renderMode{};
  renderMode.viTVmode = VI_TVMODE_NTSC_PROG;
  renderMode.fbWidth = Width;
  renderMode.efbHeight = Height;
  renderMode.xfbHeight = Height;
  renderMode.viWidth = Width;
  renderMode.viHeight = Height;
  renderMode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&renderMode);
  aurora_update();
  require(aurora_begin_frame(), "Aurora must acquire a real render frame");

  GXSetCopyClear(GXColor{16, 16, 16, 255}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, Width, Height);
  GXSetDispCopyDst(Width, Height);
  GXSetDispCopyYScale(1.0F);
  configure_draw_state();

  draw_fullscreen(-0.75F, GXColor{232, 24, 24, 255});
  const auto farId = GXAuroraRequestDepthSnapshot();
  draw_fullscreen(-0.25F, GXColor{24, 232, 24, 255});
  const auto nearId = GXAuroraRequestDepthSnapshot();

  // GXCopyDisp drains both tagged commands, then resolves and clears the EFB.
  // A third capture observes that post-copy clear without changing either
  // already sealed request.
  GXCopyDisp(nullptr, GX_TRUE);
  const auto clearedId = GXAuroraRequestDepthSnapshot();
  aurora_end_frame();

  synchronize_display_copy();
  const auto farInfo = wait_for_snapshot(farId);
  const auto nearInfo = wait_for_snapshot(nearId);
  const auto clearedInfo = wait_for_snapshot(clearedId);

  require(farInfo.frameId == nearInfo.frameId && nearInfo.frameId == clearedInfo.frameId,
          "all three exact boundaries must belong to the same frame");
  if (farInfo.width != Width || farInfo.height != Height || nearInfo.width != Width || nearInfo.height != Height ||
      clearedInfo.width != Width || clearedInfo.height != Height) {
    throw std::runtime_error("native tagged snapshot grids: far=" + std::to_string(farInfo.width) + "x" +
                             std::to_string(farInfo.height) + ", near=" + std::to_string(nearInfo.width) + "x" +
                             std::to_string(nearInfo.height) + ", cleared=" + std::to_string(clearedInfo.width) + "x" +
                             std::to_string(clearedInfo.height));
  }
  require(farInfo.viewportNear == 0.0F && farInfo.viewportFar == 1.0F,
          "snapshot metadata must retain the logical GX viewport depth range");

  u32 farZ = 0;
  u32 nearZ = 0;
  u32 clearedZ = 0;
  require(GXAuroraReadDepthSnapshotZ(farId, Width / 2, Height / 2, &farZ) == TRUE, "far boundary must be readable");
  require(GXAuroraReadDepthSnapshotZ(nearId, Width / 2, Height / 2, &nearZ) == TRUE, "near boundary must be readable");
  require(GXAuroraReadDepthSnapshotZ(clearedId, Width / 2, Height / 2, &clearedZ) == TRUE,
          "post-copy clear boundary must be readable");
  require(nearZ < farZ, "the later near draw must not alter the earlier far snapshot");
  require(clearedZ >= 0x00ffff00U, "the copy-clear continuation must contain GX far depth");

  GXAuroraReleaseDepthSnapshot(farId);
  GXAuroraReleaseDepthSnapshot(nearId);
  GXAuroraReleaseDepthSnapshot(clearedId);
}
} // namespace

int main() {
  try {
    prove_tagged_depth_boundaries();
    std::cout << "[ok] tagged depth snapshots preserve exact draw and copy-clear boundaries\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] tagged depth snapshot render proof: " << exception.what() << '\n';
    return 1;
  }
}
