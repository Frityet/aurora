#include <aurora/aurora.h>
#include <aurora/vi.hpp>
#include <dolphin/gx.h>
#include <dolphin/gx/GXAurora.h>
#include <dolphin/vi.h>
#include "gx/gx.hpp"
#include "gfx/texture.hpp"
#include "gfx/frame.hpp"
#include "gx/fifo.hpp"
#include "webgpu/gpu.hpp"
#include <array>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
constexpr u16 CopyWidth = 128;
constexpr u16 CopyHeight = 96;
void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
struct Lifetime { ~Lifetime() { aurora_shutdown(); } };
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


aurora::gx::DisplayCopySelection selected() {
  const auto state = aurora::vi::scanout_state();
  return aurora::gx::select_display_copy(state.initialized, state.black, state.frame_buffer);
}

void expect_texture_color(const aurora::gfx::TextureHandle& texture, GXColor expected,
                          u32 width = CopyWidth, u32 height = CopyHeight) {
  require(bool(texture), "completed GX copy must retain its GPU texture");
  aurora::gfx::gpu_synchronize();
  require(texture->size.width == width && texture->size.height == height,
          "selected copy must preserve configured pixel dimensions");
  // Copy one genuine GPU pixel from the same retained texture selected by
  // Aurora's presentation path. A logical VI pointer alone cannot pass this.
  const wgpu::BufferDescriptor descriptor{
      .usage = wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::MapRead, .size = 256};
  auto buffer = aurora::webgpu::g_device.CreateBuffer(&descriptor);
  auto encoder = aurora::webgpu::g_device.CreateCommandEncoder();
  const wgpu::TexelCopyTextureInfo source{
      .texture = texture->texture, .origin = {width / 2, height / 2, 0}};
  const wgpu::TexelCopyBufferInfo destination{
      .layout = {.bytesPerRow = 256, .rowsPerImage = 1}, .buffer = buffer};
  constexpr wgpu::Extent3D extent{1, 1, 1};
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  const auto command = encoder.Finish();
  aurora::webgpu::g_queue.Submit(1, &command);
  std::atomic<bool> finished{false};
  bool success = false;
  buffer.MapAsync(wgpu::MapMode::Read, 0, 256, wgpu::CallbackMode::AllowSpontaneous,
                  [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
    success = status == wgpu::MapAsyncStatus::Success;
    finished.store(true, std::memory_order_release);
  });
  while (!finished.load(std::memory_order_acquire)) {
    aurora::webgpu::g_instance.ProcessEvents();
    std::this_thread::yield();
  }
  require(success, "actual GPU scanout readback must complete");
  const auto* pixels = static_cast<const u8*>(buffer.GetConstMappedRange(0, 256));
  const bool bgra = texture->format == wgpu::TextureFormat::BGRA8Unorm ||
                    texture->format == wgpu::TextureFormat::BGRA8UnormSrgb;
  const auto r = pixels[bgra ? 2 : 0];
  const auto g = pixels[1];
  const auto b = pixels[bgra ? 0 : 2];
  std::cout << "selected GPU pixel " << unsigned(r) << ',' << unsigned(g) << ',' << unsigned(b) << '\n';
  const auto close = [](u8 a, u8 b) { return int(a) >= int(b) - 2 && int(a) <= int(b) + 2; };
  const bool matches = close(r, expected.r) && close(g, expected.g) && close(b, expected.b);
  buffer.Unmap();
  require(matches, "GX destination must retain its own copied pixels");
}

void expect_color(GXColor expected, u32 width = CopyWidth, u32 height = CopyHeight) {
  const auto selection = selected();
  require(selection.supported && selection.drawVideo && selection.copy.handle,
          "VI must select a real retained display copy");
  expect_texture_color(selection.copy.handle, expected, width, height);
}

void expect_texture_copy(const void* address, GXColor expected) {
  const auto it = aurora::gx::g_gxState.copyTextures.find(address);
  require(it != aurora::gx::g_gxState.copyTextures.end(),
          "window resize must preserve completed GXCopyTex destination contents");
  const auto* copy = it->second.live();
  require(copy && copy->submission && copy->submission->is_submitted(),
          "texture copy must still have submitted ownership");
  expect_texture_color(copy->handle, expected);
}

void empty_frame() {
  aurora_update();
  require(aurora_begin_frame(), "actual GPU frame must begin");
  aurora_end_frame();
}

void prove_scanout() {
  AuroraConfig config{};
  config.appName = "Aurora VI selected XFB proof";
#if defined(__APPLE__)
  config.desiredBackend = BACKEND_METAL;
#else
  config.desiredBackend = BACKEND_VULKAN;
#endif
  config.allowCpuAdapter = true;
  config.windowWidth = CopyWidth;
  config.windowHeight = CopyHeight;
  config.vsync = false;
  config.pauseOnFocusLost = false;
  config.logLevel = LOG_WARNING;
  const auto info = aurora_initialize(0, nullptr, &config);
  const Lifetime lifetime;
  require(info.backend == config.desiredBackend, "proof requires the selected real GPU backend");
  GXInit(nullptr, 0);
  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  VISetFrameBufferScale(1.0f);
  GXRenderModeObj mode{};
  mode.viTVmode = VI_TVMODE_NTSC_PROG;
  mode.fbWidth = mode.viWidth = CopyWidth;
  mode.efbHeight = mode.xfbHeight = mode.viHeight = CopyHeight;
  mode.xFBmode = VI_XFBMODE_SF;
  VIConfigure(&mode);
  VIInit();
  alignas(32) std::array<u8, CopyWidth * CopyHeight * 2> a{}, b{};
  alignas(32) std::array<u8, CopyWidth * CopyHeight * 4> textureCopy{};
  constexpr GXColor red{232, 24, 24, 255}, green{24, 232, 24, 255}, blue{24, 24, 232, 255};
  const auto copy = [&](void* address, GXColor color) {
    draw_quad(-1.0f, -1.0f, 1.0f, 1.0f, -0.5f, color);
    GXCopyDisp(address, GX_TRUE);
  };
  aurora_update();
  require(aurora_begin_frame(), "first real frame must begin");
  configure_draw_state();
  GXSetCopyClear(GXColor{0, 0, 0, 255}, GX_MAX_Z24);
  GXSetDispCopySrc(0, 0, CopyWidth, CopyHeight);
  GXSetDispCopyDst(CopyWidth, CopyHeight);
  GXSetDispCopyYScale(1.0f);
  copy(a.data(), red);
  copy(b.data(), green);
  draw_quad(-1.0f, -1.0f, 1.0f, 1.0f, -0.5f, blue);
  GXSetTexCopySrc(0, 0, CopyWidth, CopyHeight);
  GXSetTexCopyDst(CopyWidth, CopyHeight, GX_TF_RGBA8, GX_FALSE);
  GXCopyTex(textureCopy.data(), GX_TRUE);
  aurora_end_frame();
  require(!selected().drawVideo, "unconfigured black VI output must suppress both actual copies");

  VISetNextFrameBuffer(a.data());
  VISetBlack(FALSE);
  VIFlush();
  VIWaitForRetrace();
  empty_frame();
  require(aurora::gx::latest_display_copy() == nullptr, "new frame must have no newest-copy fallback");
  expect_color(red);
  VISetNextFrameBuffer(b.data());
  VIWaitForRetrace();
  empty_frame();
  expect_color(red);
  VIFlush();
  VIWaitForRetrace();
  empty_frame();
  expect_color(green);

  // Completed copies represent guest-owned memory. Changing the host surface
  // or EFB allocation must not discard either XFB, or a texture-copy source.
  // Read from the original destination maps again after each resize, so a
  // test-owned shared pointer cannot accidentally preserve a lost owner.
  const auto efbSize = aurora::webgpu::g_frameBuffer.size;
  const auto surface = aurora::webgpu::g_graphicsConfig.surfaceConfiguration;
  const auto verify_copies = [&] {
    expect_color(green);
    VISetNextFrameBuffer(a.data());
    VIFlush();
    VIWaitForRetrace();
    expect_color(red);
    VISetNextFrameBuffer(b.data());
    VIFlush();
    VIWaitForRetrace();
    expect_color(green);
    expect_texture_copy(textureCopy.data(), blue);
  };
  expect_texture_copy(textureCopy.data(), blue);
  aurora::webgpu::resize_swapchain(efbSize.width, efbSize.height, surface.width + 64, surface.height + 48);
  verify_copies();
  require(aurora::gx::g_gxState.copyTextureCache.empty(),
          "resize must discard reusable allocations when no current-frame copy needs its key");
  std::cout << "[ok] surface-only resize preserves both XFBs and GXCopyTex GPU pixels\n";

  // A resize must also preserve the current-frame fallback/readback identity.
  // It is deliberately a different color from the older destination A.
  require(aurora_begin_frame(), "current-frame copy proof must begin");
  configure_draw_state();
  copy(b.data(), green);
  aurora_end_frame();
  const auto currentKey = aurora::gx::g_gxState.frameDisplayCopyKey;
  aurora::webgpu::resize_swapchain(efbSize.width + 32, efbSize.height + 24,
                                  surface.width + 64, surface.height + 48);
  verify_copies();
  require(aurora::gx::g_gxState.copyTextureCache.size() == 1 &&
              aurora::gx::g_gxState.copyTextureCache.contains(currentKey),
          "resize must keep only the current-frame reuse key");
  const auto* latest = aurora::gx::latest_display_copy();
  require(latest && latest->submission && latest->submission->is_submitted(),
          "resize must preserve current-frame display-copy submission ownership");
  expect_texture_color(latest->handle, green);
  std::cout << "[ok] EFB-size change preserves both XFBs and GXCopyTex GPU pixels\n";

  // A new scaled copy to the same guest address must obtain the new size,
  // while the other two destinations retain their original dimensions/pixels.
  AuroraSetViewportPolicy(AURORA_VIEWPORT_STRETCH);
  aurora_update();
  const u32 scaledWidth = efbSize.width + 32;
  const u32 scaledHeight = efbSize.height + 24;
  aurora::webgpu::resize_swapchain(scaledWidth, scaledHeight, surface.width + 64, surface.height + 48);
  const auto oldSelectedTexture = selected().copy.handle;
  require(aurora_begin_frame(), "scaled copy proof frame must begin");
  configure_draw_state();
  copy(b.data(), green);
  aurora_end_frame();
  require(selected().copy.handle != oldSelectedTexture,
          "a changed-size copy must not reuse the old destination allocation");
  expect_color(green, scaledWidth, scaledHeight);
  VISetNextFrameBuffer(a.data());
  VIFlush();
  VIWaitForRetrace();
  expect_color(red);
  expect_texture_copy(textureCopy.data(), blue);
  VISetNextFrameBuffer(b.data());
  VIFlush();
  VIWaitForRetrace();
  for (u32 delta = 1; delta <= 8; ++delta) {
    aurora::webgpu::resize_swapchain(scaledWidth + delta, scaledHeight + delta,
                                    surface.width + 64 + delta, surface.height + 48 + delta);
    require(aurora::gx::g_gxState.copyTextureCache.size() == 1,
            "repeated resize must not accumulate allocation-size variants");
    expect_color(green, scaledWidth + delta - 1, scaledHeight + delta - 1);
    require(aurora_begin_frame(), "repeated scaled copy frame must begin");
    configure_draw_state();
    copy(b.data(), green);
    aurora_end_frame();
    expect_color(green, scaledWidth + delta, scaledHeight + delta);
    require(aurora::gx::g_gxState.copyTextureCache.size() <= 2,
            "new size may add one reusable allocation until the next resize");
  }
  std::cout << "[ok] new scaled copy changes allocation and repeated resize bounds reuse cache\n";
  AuroraSetViewportPolicy(AURORA_VIEWPORT_NATIVE);
  aurora_update();
  aurora::webgpu::resize_swapchain(efbSize.width, efbSize.height, surface.width, surface.height);
  require(aurora_begin_frame(), "native copy restoration frame must begin");
  configure_draw_state();
  copy(b.data(), green);
  aurora_end_frame();
  empty_frame();
  verify_copies();

  // Replace a submitted XFB in an unsubmitted encoder, then abandon it. A
  // retained native texture alone cannot establish the correct pixel owner.
  require(aurora_begin_frame(), "abort proof frame must begin");
  copy(b.data(), blue);
  const auto pending = aurora::gx::display_copy_for_frame_buffer(b.data())->submission;
  require(pending && !pending->is_submitted(), "replacement must still be pending");
  const auto snapshot = GXAuroraRequestDepthSnapshot();
  aurora::gx::fifo::drain();
  GXAbortFrame();
  GXDrawDone();
  require(pending->abandoned(), "aborted replacement must lose submission ownership");
  AuroraDepthSnapshotInfo snapshotInfo{};
  require(GXAuroraGetDepthSnapshotInfo(snapshot, &snapshotInfo) == AURORA_DEPTH_SNAPSHOT_DROPPED,
          "aborted depth request must not report completion");
  GXAuroraReleaseDepthSnapshot(snapshot);
  expect_color(green);

  // The preceding submitted prefix survives another abort of its replacement.
  copy(b.data(), blue);
  GXDrawDone();
  const auto submitted = aurora::gx::display_copy_for_frame_buffer(b.data())->submission;
  require(submitted && submitted->is_submitted(), "draw-done must commit the original prefix");
  copy(b.data(), red);
  GXAbortFrame();
  GXDrawDone();
  require(submitted->is_submitted() && !submitted->abandoned(), "abort must preserve submitted ownership");
  expect_color(blue);
  aurora_end_frame();

  VISetBlack(TRUE);
  VIFlush();
  VIWaitForRetrace();
  empty_frame();
  require(selected().supported && !selected().drawVideo && !selected().copy.handle,
          "black VI output must not sample a cached display texture");
  GXDestroyCopyTex(b.data());
  GXDrawDone();
  require(aurora::gx::display_copy_for_frame_buffer(b.data()) == nullptr,
          "buffer retirement must remove its display-copy ownership");
  VISetBlack(FALSE);
  VIFlush();
  VIWaitForRetrace();
  require(!selected().supported, "a retired or CPU-only XFB must fail explicitly until copied by GX");

  // Replace the retired buffer at exactly the same address. No stale cached
  // texture or latest-frame fallback may impersonate its new GPU contents.
  require(aurora_begin_frame(), "buffer reuse frame must begin");
  copy(b.data(), blue);
  aurora_end_frame();
  expect_color(blue);
  VISetNextFrameBuffer(a.data());
  VIFlush();
  VIWaitForRetrace();
  empty_frame();
  expect_color(red);
  GXDestroyCopyTex(a.data());
  GXDestroyCopyTex(b.data());
  GXDestroyCopyTex(textureCopy.data());
  aurora::vi::shutdown();
}
} // namespace
int main() {
  try {
    prove_scanout();
    std::cout << "[ok] real GX copies survive resize and follow VI flush, retrace, black, retirement and reuse\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "[fail] VI scanout: " << e.what() << '\n';
    return 1;
  }
}
