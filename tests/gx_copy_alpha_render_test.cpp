#include <aurora/aurora.h>
#include <aurora/gfx.hpp>

#include "../lib/gx/gx.hpp"
#include "../lib/gfx/texture.hpp"
#include "../lib/webgpu/map_future.hpp"

#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr auto Width = std::uint16_t{64};
constexpr auto Height = std::uint16_t{48};
constexpr u32 Stride = Width * 4;

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


std::vector<u8> read_copy(const void* destination) {
  // Submit all recorded GX passes before enqueueing the GPU readback.
  aurora::gfx::synchronize();
  const auto found = aurora::gx::g_gxState.copyTextures.find(destination);
  require(found != aurora::gx::g_gxState.copyTextures.end(), "GXCopyTex must produce a texture");
  const auto& texture = found->second.handle;
  const auto device = aurora::gfx::device();
  const wgpu::BufferDescriptor bufferDesc{
      .label = "GX copy alpha readback",
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead,
      .size = Stride * Height,
  };
  const auto buffer = device.CreateBuffer(&bufferDesc);
  const auto encoder = device.CreateCommandEncoder();
  const wgpu::TexelCopyTextureInfo source{.texture = texture->texture};
  const wgpu::TexelCopyBufferInfo target{
      .layout = {.bytesPerRow = Stride, .rowsPerImage = Height}, .buffer = buffer,
  };
  const wgpu::Extent3D extent{Width, Height, 1};
  encoder.CopyTextureToBuffer(&source, &target, &extent);
  const auto commands = encoder.Finish();
  aurora::gfx::queue().Submit(1, &commands);
  bool mapped = false;
  auto future = buffer.MapAsync(wgpu::MapMode::Read, 0, Stride * Height, wgpu::CallbackMode::WaitAnyOnly,
                                     [&mapped](wgpu::MapAsyncStatus status, wgpu::StringView) {
                                       mapped = status == wgpu::MapAsyncStatus::Success;
                                     });
  aurora::webgpu::complete_future(future, true);
  require(mapped, "GX texture-copy pixel readback must complete");
  std::vector<u8> pixels(Stride * Height);
  std::memcpy(pixels.data(), buffer.GetConstMappedRange(0, pixels.size()), pixels.size());
  buffer.Unmap();
  if (texture->format == wgpu::TextureFormat::BGRA8Unorm ||
      texture->format == wgpu::TextureFormat::BGRA8UnormSrgb) {
    for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
  }
  return pixels;
}

std::array<u8, 4> pixel(const std::vector<u8>& pixels, u32 x, u32 y) {
  const auto offset = y * Stride + x * 4;
  return {pixels[offset], pixels[offset + 1], pixels[offset + 2], pixels[offset + 3]};
}

void begin_frame() {
  aurora_update();
  require(aurora_begin_frame(), "Aurora must acquire a real render frame");
  configure_draw_state();
  GXSetTexCopySrc(0, 0, Width, Height);
  GXSetTexCopyDst(Width, Height, GX_TF_RGBA8, GX_FALSE);
  GXSetCopyFilter(GX_FALSE, nullptr, GX_FALSE, nullptr);
}

void draw_half(bool left, GXColor color) {
  GXSetScissor(left ? 0 : Width / 2, 0, Width / 2, Height);
  draw_fullscreen(color);
  GXSetScissor(0, 0, Width, Height);
}

void prove_copy_preservation() {
  // These are real client-owned copy destinations. Their contents are retained
  // on the GPU, just as for ordinary GX texture copies in Aurora.
  alignas(32) std::array<u8, Stride * Height> first{};
  alignas(32) std::array<u8, Stride * Height> second{};
  for (const bool clear : {false, true}) {
    begin_frame();
    draw_half(true, {252, 0, 0, 84});
    draw_half(false, {0, 252, 0, 168});
    GXSetCopyClear({16, 32, 48, 212}, GX_MAX_Z24);
    // State changes after a draw cannot rewrite its pixels during a copy.
    GXSetDstAlpha(GX_TRUE, 40);
    GXCopyTex(first.data(), clear ? GX_TRUE : GX_FALSE);
    GXSetDstAlpha(GX_FALSE, 0);
    GXCopyTex(second.data(), GX_FALSE);
    aurora_end_frame();
    const auto copied = read_copy(first.data());
    const auto remaining = read_copy(second.data());
    require_color(pixel(copied, Width / 4, Height / 2), {252, 0, 0, 84}, "copied left region");
    require_color(pixel(copied, 3 * Width / 4, Height / 2), {0, 252, 0, 168}, "copied right region");
    require_color(pixel(remaining, Width / 4, Height / 2), clear ? std::array<u8, 4>{16, 32, 48, 212}
                                                              : std::array<u8, 4>{252, 0, 0, 84},
                  clear ? "EFB left uses independent copy-clear alpha" : "no-clear preserves left EFB alpha");
    require_color(pixel(remaining, 3 * Width / 4, Height / 2), clear ? std::array<u8, 4>{16, 32, 48, 212}
                                                                  : std::array<u8, 4>{0, 252, 0, 168},
                  clear ? "EFB right uses independent copy-clear alpha" : "no-clear preserves right EFB alpha");
  }
}

void prove_fragment_alpha() {
  alignas(32) std::array<u8, Stride * Height> destination{};
  begin_frame();
  draw_fullscreen({0, 0, 255, 212});
  GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_COPY);
  GXSetDstAlpha(GX_TRUE, 168);
  draw_half(true, {255, 0, 0, 84});
  GXSetDstAlpha(GX_TRUE, 84);
  draw_half(false, {0, 255, 0, 0});
  // Neither a failing alpha test nor disabled alpha writes may apply dstAlpha.
  GXSetAlphaCompare(GX_GREATER, 128, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetDstAlpha(GX_TRUE, 252);
  draw_fullscreen({255, 255, 255, 84});
  GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GXSetAlphaUpdate(GX_FALSE);
  GXSetScissor(0, 0, Width, 4);
  draw_fullscreen({255, 255, 255, 255});
  GXSetScissor(0, 0, Width, Height);
  GXSetAlphaUpdate(GX_TRUE);
  // Read after disabling the override so copy-time repair cannot mask a
  // broken draw-time implementation.
  GXSetDstAlpha(GX_FALSE, 0);
  GXCopyTex(destination.data(), GX_FALSE);
  aurora_end_frame();
  const auto result = read_copy(destination.data());
  require_color(pixel(result, Width / 4, Height / 2), {84, 0, 171, 168},
                "TEV alpha blends RGB while dstAlpha replaces fragment alpha");
  require_color(pixel(result, 3 * Width / 4, Height / 2), {0, 0, 255, 84},
                "zero TEV alpha still writes destination alpha");
  require_color(pixel(result, Width / 4, 2), {255, 255, 255, 168}, "disabled alpha writes preserve left alpha");
  require_color(pixel(result, 3 * Width / 4, 2), {255, 255, 255, 84}, "disabled alpha writes preserve right alpha");

  begin_frame();
  draw_fullscreen({0, 0, 255, 212});
  GXSetBlendMode(GX_BM_BLEND, GX_BL_INVSRCALPHA, GX_BL_SRCALPHA, GX_LO_COPY);
  GXSetDstAlpha(GX_TRUE, 168);
  draw_fullscreen({255, 0, 0, 84});
  GXSetDstAlpha(GX_FALSE, 0);
  GXCopyTex(destination.data(), GX_FALSE);
  aurora_end_frame();
  require_color(pixel(read_copy(destination.data()), Width / 2, Height / 2), {171, 0, 84, 168},
                "both blend factors retain the original source alpha");

  begin_frame();
  GXSetDstAlpha(GX_TRUE, 168);
  draw_half(true, {252, 0, 0, 0});
  GXSetDstAlpha(GX_TRUE, 84);
  draw_half(false, {0, 252, 0, 255});
  GXSetDstAlpha(GX_TRUE, 212);
  GXSetColorUpdate(GX_FALSE);
  GXSetScissor(0, 0, Width, 4);
  draw_fullscreen({255, 255, 255, 0});
  GXSetScissor(0, 0, Width, Height);
  GXSetColorUpdate(GX_TRUE);
  GXSetDstAlpha(GX_FALSE, 0);
  GXCopyTex(destination.data(), GX_FALSE);
  aurora_end_frame();
  const auto unblended = read_copy(destination.data());
  require_color(pixel(unblended, Width / 4, Height / 2), {252, 0, 0, 168}, "unblended left override");
  require_color(pixel(unblended, 3 * Width / 4, Height / 2), {0, 252, 0, 84}, "unblended right override");
  require_color(pixel(unblended, Width / 4, 2), {252, 0, 0, 212}, "alpha-only draw preserves left RGB");
  require_color(pixel(unblended, 3 * Width / 4, 2), {0, 252, 0, 212}, "alpha-only draw preserves right RGB");
}

void prove_destination_alpha(std::string_view mode) {
  AuroraConfig config{};
  config.appName = "Aurora GX destination-alpha render proof";
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


  if (mode != "--draw-only") prove_copy_preservation();
  if (mode != "--copy-only") prove_fragment_alpha();
}
} // namespace

int main(int argc, char** argv) {
  try {
    const std::string_view mode = argc > 1 ? argv[1] : "";
    require(argc <= 2 && (mode.empty() || mode == "--draw-only" || mode == "--copy-only"),
            "expected optional --copy-only or --draw-only");
    prove_destination_alpha(mode);
    std::cout << "[ok] GX per-fragment destination alpha and independent texture-copy/clear pixels\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "[fail] GX destination-alpha proof: " << exception.what() << '\n';
    return 1;
  }
}
