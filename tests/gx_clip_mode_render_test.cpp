#include <aurora/aurora.h>

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

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

void draw_indexed_quad(const std::array<std::array<float, 3>, 4>& positions, GXColor color) {
  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_INDEX8);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GXSetArray(GX_VA_POS, positions.data(), sizeof(positions[0]));
  GXBegin(GX_QUADS, GX_VTXFMT0, 4);
  for (u8 index = 0; index < positions.size(); ++index) {
    GXPosition1x8(index);
    GXColor4u8(color.r, color.g, color.b, color.a);
  }
  GXEnd();

  GXClearVtxDesc();
  GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
  GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
}

[[nodiscard]] std::vector<std::uint8_t> read_display_copy() {
  auto width = 0U;
  auto height = 0U;
  const auto hasDisplayCopy = AuroraGetDisplayCopySize(&width, &height);
  if (hasDisplayCopy != GX_TRUE || width != CopyWidth || height != CopyHeight) {
    std::cerr << "display copy available=" << hasDisplayCopy << " size=" << width << 'x' << height << '\n';
  }
  require(hasDisplayCopy == GX_TRUE && width == CopyWidth && height == CopyHeight,
          "GXCopyDisp must materialize the configured display texture");

  auto stride = 0U;
  auto pixels = std::vector<std::uint8_t>(static_cast<std::size_t>(width) * height * 4U);
  require(AuroraReadDisplayCopyRGBA8(pixels.data(), static_cast<u32>(pixels.size()), &width, &height, &stride) ==
                  GX_TRUE &&
              stride == width * 4U,
          "the completed clipping frame must be readable as RGBA8");
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

void prove_clip_mode() {
  auto config = AuroraConfig{};
  config.appName = "Aurora GX clip mode render proof";
#if defined(__APPLE__)
  config.desiredBackend = BACKEND_METAL;
#else
  config.desiredBackend = BACKEND_VULKAN;
#endif
  config.allowCpuAdapter = true;
  config.windowWidth = CopyWidth; config.windowHeight = CopyHeight;
  config.vsync = false; config.pauseOnFocusLost = false;
  config.logCallback = &log_callback; config.logLevel = LOG_WARNING;
  const auto info = aurora_initialize(0, nullptr, &config);
  const auto lifetime = AuroraLifetime{};
  require(info.backend == config.desiredBackend, "The clipping proof requires the requested real GPU backend.");
  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  GXInit(nullptr, 0);
  VISetFrameBufferScale(1.0F);
  GXRenderModeObj mode{};
  mode.viTVmode = VI_TVMODE_NTSC_PROG; mode.fbWidth = CopyWidth;
  mode.efbHeight = CopyHeight; mode.xfbHeight = CopyHeight;
  mode.viWidth = CopyWidth; mode.viHeight = CopyHeight; mode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&mode);
  struct Case { const char* name; float leftZ, rightZ; bool outsideViewport; bool matrixIndex; bool indexedPosition = false; };
  const auto cases = std::array{
      Case{"near crossing", -0.5F, 0.5F, false, false},
      Case{"far crossing", -0.5F, -1.5F, false, false},
      Case{"near reject", 0.5F, 0.5F, false, false},
      Case{"far reject", -1.5F, -1.5F, false, false},
      Case{"viewport guardband", -0.5F, -0.5F, true, false},
      Case{"indexed position matrix", -0.5F, 0.5F, false, true},
      Case{"indexed position array", -0.5F, 0.5F, false, false, true}};
  constexpr auto Blue = GXColor{24, 24, 232, 255};
  constexpr auto Green = GXColor{24, 232, 24, 255};
  for (const auto& test : cases) {
    for (const auto topology : {GX_QUADS, GX_TRIANGLES, GX_TRIANGLESTRIP, GX_TRIANGLEFAN, static_cast<GXPrimitive>(0xff)}) {
    for (const auto clipMode : {GX_CLIP_ENABLE, GX_CLIP_DISABLE}) {
      aurora_update();
      require(aurora_begin_frame(), "Aurora must acquire the clipping test frame.");
      GXSetCopyClear(GXColor{24,24,24,255}, GX_MAX_Z24);
      GXSetDispCopySrc(0,0,CopyWidth,CopyHeight); GXSetDispCopyDst(CopyWidth,CopyHeight);
      GXSetDispCopyYScale(1.0F);
      configure_draw_state();
      GXSetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
      GXSetClipMode(clipMode);
      if (test.outsideViewport) GXSetViewport(64,0,128,CopyHeight,0,1);
      const float right = test.outsideViewport ? 2.0F : 0.8F;
      const std::array<std::array<float,3>,4> vertices{{
          {-0.8F,-0.8F,test.leftZ}, {right,-0.8F,test.rightZ},
          {right,0.8F,test.rightZ}, {-0.8F,0.8F,test.leftZ}}};
      if (test.matrixIndex) {
        // All source positions have x/z zero: each original per-vertex matrix
        // supplies both displacement and clipping depth. Current matrix 0 is
        // deliberately different, so rejection cannot use a global shortcut.
        GXSetVtxDesc(GX_VA_PNMTXIDX, GX_DIRECT);
        for (u32 i=0; i<vertices.size(); ++i) {
          const float matrix[3][4] = {{1,0,0,vertices[i][0]}, {0,1,0,0}, {0,0,1,vertices[i][2]}};
          GXLoadPosMtxImm(matrix, (i+1)*3);
        }
      }
      if (test.indexedPosition) {
        GXSetVtxDesc(GX_VA_POS,GX_INDEX8);
        GXSetArray(GX_VA_POS,vertices.data(),sizeof(vertices[0]));
      }
      const auto order = topology == GX_TRIANGLES ? std::vector<u32>{0,1,2,2,3,0}
          : topology == GX_TRIANGLESTRIP ? std::vector<u32>{1,2,0,3} : std::vector<u32>{0,1,2,3};
      const std::array<u16,6> indices{0,1,2,2,3,0};
      if (topology==static_cast<GXPrimitive>(0xff)) GXBeginIndexed(GX_VTXFMT0,4,indices.data(),indices.size());
      else GXBegin(topology,GX_VTXFMT0,static_cast<u16>(order.size()));
      for (const auto i : order) {
        if (test.matrixIndex) GXPosition1x8((i+1)*3); // One FIFO byte for PNMTXIDX.
        if (test.indexedPosition) GXPosition1x8(i);
        else GXPosition3f32(test.matrixIndex ? 0.0F : vertices[i][0], vertices[i][1],
                      test.matrixIndex ? 0.0F : vertices[i][2]);
        GXColor4u8(Blue.r,Blue.g,Blue.b,Blue.a);
      }
      GXEnd();
      GXSetVtxDesc(GX_VA_PNMTXIDX,GX_NONE);
      GXSetVtxDesc(GX_VA_POS,GX_DIRECT);
      // Re-enabling must immediately use the original authored viewport. No
      // GXSetViewport intervenes, so this also proves per-draw restoration.
      GXSetClipMode(GX_CLIP_ENABLE);
      draw_quad(-0.2F,0.86F,0.2F,0.98F,-0.5F,Green);
      GXFlush(); GXCopyDisp(nullptr,GX_TRUE); aurora_end_frame();
      const auto pixels = read_display_copy();
      const auto outsideX = test.outsideViewport ? 224U : 179U;
      const bool rejected = test.leftZ == test.rightZ && !test.outsideViewport;
      const auto expected = clipMode == GX_CLIP_DISABLE && !rejected ? DominantColor::Blue : DominantColor::Background;
      if (!neighborhood_is(pixels,outsideX,CopyHeight/2,expected)) {
        const auto offset=(CopyHeight/2*CopyWidth+outsideX)*4;
        std::cerr << test.name << " mode=" << clipMode << " topology=" << topology << " sample=" << int(pixels[offset]) << ','
                  << int(pixels[offset+1]) << ',' << int(pixels[offset+2]) << '\n';
        throw std::runtime_error("Clipping/rejection/viewport pixel mismatch.");
      }
      require(neighborhood_is(pixels,CopyWidth/2,8,DominantColor::Green) ||
                  neighborhood_is(pixels,CopyWidth/2,CopyHeight-8,DominantColor::Green),
              "Re-enabling clipping failed to restore the authored viewport for the next draw.");
      std::cout << "[ok] " << test.name << " mode=" << clipMode << " topology=" << topology << '\n';
    }
    }
  }

  // Same-sized viewports moved only vertically must publish a fresh transform
  // uniform. Keep both original draws in one command stream to exercise cache
  // invalidation; returning to a normal draw must not inherit full-target state.
  aurora_update(); require(aurora_begin_frame(), "Viewport regression frame acquisition failed.");
  configure_draw_state(); GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE); GXSetClipMode(GX_CLIP_DISABLE);
  GXSetViewport(0,0,CopyWidth,80,0,1);
  draw_quad(-0.8F,-0.8F,0.8F,0.8F,-0.5F,Blue);
  GXSetViewport(0,112,CopyWidth,80,0,1);
  draw_quad(-0.8F,-0.8F,0.8F,0.8F,-0.5F,Green);
  GXFlush(); GXCopyDisp(nullptr,GX_TRUE); aurora_end_frame();
  auto pixels=read_display_copy();
  require((neighborhood_is(pixels,128,40,DominantColor::Blue) && neighborhood_is(pixels,128,152,DominantColor::Green)) ||
          (neighborhood_is(pixels,128,40,DominantColor::Green) && neighborhood_is(pixels,128,152,DominantColor::Blue)),
          "A top-only viewport change reused the prior clip-disabled transform.");
  std::cout << "[ok] top-only viewport change preserves separate draw transforms\n";

  // The offscreen target changes scale denominators even when its default
  // viewport equals the previous EFB viewport. Force an ordinary light uniform
  // refresh offscreen, then restore without another GX viewport command.
  aurora_update(); require(aurora_begin_frame(), "Offscreen regression frame acquisition failed.");
  configure_draw_state(); GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE); GXSetClipMode(GX_CLIP_DISABLE);
  GXSetViewport(0,0,128,96,0,1);
  draw_quad(-0.8F,-0.8F,0.8F,0.8F,-0.5F,Blue);
  GXCreateFrameBuffer(128,96);
  GXSetChanMatColor(GX_COLOR0A0,GXColor{255,255,255,255});
  draw_quad(-0.8F,-0.8F,0.8F,0.8F,-0.5F,Green);
  GXRestoreFrameBuffer();
  draw_quad(0.0F,-0.8F,0.8F,0.8F,-0.5F,Green);
  GXFlush(); GXCopyDisp(nullptr,GX_TRUE); aurora_end_frame();
  pixels=read_display_copy();
  const bool topPair=neighborhood_is(pixels,32,48,DominantColor::Blue) && neighborhood_is(pixels,96,48,DominantColor::Green);
  const bool bottomPair=neighborhood_is(pixels,32,CopyHeight-48,DominantColor::Blue) && neighborhood_is(pixels,96,CopyHeight-48,DominantColor::Green);
  require(topPair || bottomPair,"Offscreen return reused another target's clip-disabled viewport transform.");
  std::cout << "[ok] offscreen target transition restores authored EFB viewport and transform\n";

  // A triangle crossing the eye plane must retain full homogeneous clipping.
  // Compare real rendered pixels in both modes, with one vertex behind the eye.
  std::vector<u8> enabledEye;
  for (const auto clipMode : {GX_CLIP_ENABLE,GX_CLIP_DISABLE}) {
    aurora_update(); require(aurora_begin_frame(), "Eye-plane frame acquisition failed.");
    configure_draw_state(); GXSetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE); GXSetClipMode(clipMode);
    const float perspective[4][4]={{1,0,0,0},{0,1,0,0},{0,0,-1.0F/9,-10.0F/9},{0,0,-1,0}};
    GXSetProjection(perspective,GX_PERSPECTIVE);
    GXBegin(GX_TRIANGLES,GX_VTXFMT0,3);
    for (const auto& vertex : std::array{std::array{-0.8F,-0.8F,-2.0F},std::array{0.8F,-0.8F,-2.0F},std::array{0.0F,0.8F,1.0F}}) {
      GXPosition3f32(vertex[0],vertex[1],vertex[2]); GXColor4u8(Blue.r,Blue.g,Blue.b,Blue.a);
    }
    GXEnd(); GXFlush(); GXCopyDisp(nullptr,GX_TRUE); aurora_end_frame();
    pixels=read_display_copy();
    if (clipMode==GX_CLIP_ENABLE) enabledEye=pixels;
    else require(enabledEye==pixels,"Eye-plane crossing diverged between enabled and disabled clipping.");
  }
  std::size_t bluePixels=0;
  for (std::size_t i=0;i<pixels.size();i+=4) bluePixels+=is_color(std::span<const u8,4>{pixels.data()+i,4},DominantColor::Blue);
  require(bluePixels>100,"Eye-plane equivalence must actually rasterize the surviving polygon.");
  std::cout << "[ok] eye-plane crossing produces identical surviving polygon pixels\n";
}
} // namespace
int main() {
  try { prove_clip_mode(); return 0; }
  catch (const std::exception& exception) {
    std::cerr << "[fail] GX clip mode: " << exception.what() << '\n'; return 1;
  }
}
