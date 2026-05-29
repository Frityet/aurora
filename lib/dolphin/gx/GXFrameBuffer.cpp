#include "gx.hpp"
#include "__gx.h"

#include "../../gfx/tex_copy_conv.hpp"
#include "../../gfx/texture.hpp"
#include "../../window.hpp"
#include "../../gfx/clear.hpp"
#include "../../webgpu/gpu.hpp"
#include "../vi/vi_internal.hpp"

#include <algorithm>
#include <cmath>
#include <atomic>
#include <cstring>
#include <thread>

namespace {
aurora::Vec2<uint32_t> scale_copy_dst(u32 logicalWidth, u32 logicalHeight) {
  if (g_gxState.viewportPolicy == AURORA_VIEWPORT_NATIVE) {
    return {logicalWidth, logicalHeight};
  }

  const auto [logicalFbWidth, logicalFbHeight] = aurora::gx::logical_fb_size();
  const auto [targetWidth, targetHeight] = aurora::gfx::get_render_target_size();
  if (logicalFbWidth == 0 || logicalFbHeight == 0 || targetWidth == 0 || targetHeight == 0) {
    return {logicalWidth, logicalHeight};
  }

  const float scaleX = static_cast<float>(targetWidth) / static_cast<float>(logicalFbWidth);
  const float scaleY = static_cast<float>(targetHeight) / static_cast<float>(logicalFbHeight);
  const auto scaledWidth = std::max<u32>(static_cast<u32>(std::lround(static_cast<float>(logicalWidth) * scaleX)), 1);
  const auto scaledHeight = std::max<u32>(static_cast<u32>(std::lround(static_cast<float>(logicalHeight) * scaleY)), 1);
  return {scaledWidth, scaledHeight};
}

u32 display_copy_y_scale(f32 verticalScale) {
  if (verticalScale <= 0.f || !std::isfinite(verticalScale)) {
    return 0x100;
  }
  return static_cast<u32>(256.f / verticalScale) & 0x1FF;
}

u32 scaled_xfb_lines(u32 efbHeight, u32 iScale) {
  iScale = std::max(iScale, 1u);
  u32 count = (std::max(efbHeight, 1u) - 1u) * 256u;
  u32 realHeight = count / iScale + 1u;
  u32 scaleDenominator = iScale;
  if (scaleDenominator > 0x80u && scaleDenominator < 0x100u) {
    while ((scaleDenominator & 1u) == 0u) {
      scaleDenominator >>= 1u;
    }
    if (scaleDenominator != 0u && efbHeight % scaleDenominator == 0u) {
      ++realHeight;
    }
  }
  return std::min(realHeight, 1024u);
}

bool gpu_copy_ready() {
  return aurora::webgpu::g_device && aurora::webgpu::g_queue;
}

u32 align_copy_row_pitch(u32 bytesPerRow) {
  constexpr u32 kTextureBytesPerRowAlignment = 256;
  return (bytesPerRow + kTextureBytesPerRowAlignment - 1u) & ~(kTextureBytesPerRowAlignment - 1u);
}
} // namespace

namespace aurora::gx {
const GXState::CopyTextureRef* display_copy_for_present() noexcept {
  if (!g_gxState.frameDisplayCopyValid) {
    return nullptr;
  }
  const auto it = g_gxState.copyTextureCache.find(g_gxState.frameDisplayCopyKey);
  if (it == g_gxState.copyTextureCache.end() || !it->second.handle) {
    return nullptr;
  }
  return &it->second;
}

void clear_frame_display_copy() noexcept {
  g_gxState.frameDisplayCopyValid = false;
  g_gxState.frameDisplayCopyKey = {};
}

bool has_display_copy() noexcept { return display_copy_for_present() != nullptr; }

bool display_copy_size(u32* width, u32* height) noexcept {
  const auto* displayCopy = display_copy_for_present();
  if (displayCopy == nullptr || !displayCopy->handle) {
    if (width != nullptr) {
      *width = 0;
    }
    if (height != nullptr) {
      *height = 0;
    }
    return false;
  }

  if (width != nullptr) {
    *width = displayCopy->handle->size.width;
  }
  if (height != nullptr) {
    *height = displayCopy->handle->size.height;
  }
  return true;
}

bool read_display_copy_rgba8(void* dst, u32 dstSize, u32* width, u32* height, u32* rowStrideOut) noexcept {
  const auto* displayCopy = display_copy_for_present();
  if (displayCopy == nullptr || !displayCopy->handle || !gpu_copy_ready()) {
    display_copy_size(width, height);
    if (rowStrideOut != nullptr) {
      *rowStrideOut = 0;
    }
    return false;
  }

  const auto& handle = displayCopy->handle;
  const u32 copyWidth = handle->size.width;
  const u32 copyHeight = handle->size.height;
  const u32 tightRowStride = copyWidth * 4u;
  if (width != nullptr) {
    *width = copyWidth;
  }
  if (height != nullptr) {
    *height = copyHeight;
  }
  if (rowStrideOut != nullptr) {
    *rowStrideOut = tightRowStride;
  }
  if (dst == nullptr) {
    return true;
  }
  if (copyWidth == 0 || copyHeight == 0 || dstSize < tightRowStride * copyHeight) {
    return false;
  }

  const u32 readbackRowPitch = align_copy_row_pitch(tightRowStride);
  const u64 readbackSize = static_cast<u64>(readbackRowPitch) * copyHeight;
  const wgpu::BufferDescriptor readbackDescriptor{
      .label = "Aurora display copy readback buffer",
      .usage = wgpu::BufferUsage::MapRead | wgpu::BufferUsage::CopyDst,
      .size = readbackSize,
  };
  auto readbackBuffer = webgpu::g_device.CreateBuffer(&readbackDescriptor);
  if (!readbackBuffer) {
    return false;
  }

  const wgpu::CommandEncoderDescriptor encoderDescriptor{
      .label = "Aurora display copy readback encoder",
  };
  auto encoder = webgpu::g_device.CreateCommandEncoder(&encoderDescriptor);
  const wgpu::TexelCopyTextureInfo source{
      .texture = handle->texture,
  };
  const wgpu::TexelCopyBufferInfo destination{
      .layout =
          wgpu::TexelCopyBufferLayout{
              .bytesPerRow = readbackRowPitch,
              .rowsPerImage = copyHeight,
          },
      .buffer = readbackBuffer,
  };
  const wgpu::Extent3D extent{
      .width = copyWidth,
      .height = copyHeight,
      .depthOrArrayLayers = 1,
  };
  encoder.CopyTextureToBuffer(&source, &destination, &extent);
  const wgpu::CommandBufferDescriptor commandBufferDescriptor{
      .label = "Aurora display copy readback command buffer",
  };
  const auto commandBuffer = encoder.Finish(&commandBufferDescriptor);
  webgpu::g_queue.Submit(1, &commandBuffer);

  std::atomic_bool mapped{false};
  std::atomic_bool mapSucceeded{false};
  readbackBuffer.MapAsync(wgpu::MapMode::Read, 0, readbackSize, wgpu::CallbackMode::AllowSpontaneous,
                          [&](wgpu::MapAsyncStatus status, wgpu::StringView) {
                            mapSucceeded.store(status == wgpu::MapAsyncStatus::Success, std::memory_order_release);
                            mapped.store(true, std::memory_order_release);
                          });
  while (!mapped.load(std::memory_order_acquire)) {
    webgpu::g_instance.ProcessEvents();
    std::this_thread::yield();
  }
  if (!mapSucceeded.load(std::memory_order_acquire)) {
    return false;
  }

  const auto* mappedBytes = static_cast<const u8*>(readbackBuffer.GetConstMappedRange(0, readbackSize));
  if (mappedBytes == nullptr) {
    readbackBuffer.Unmap();
    return false;
  }

  auto* dstBytes = static_cast<u8*>(dst);
  const bool needsBgraSwizzle = handle->format == wgpu::TextureFormat::BGRA8Unorm ||
                               handle->format == wgpu::TextureFormat::BGRA8UnormSrgb;
  for (u32 y = 0; y < copyHeight; ++y) {
    const auto* srcRow = mappedBytes + static_cast<size_t>(y) * readbackRowPitch;
    auto* dstRow = dstBytes + static_cast<size_t>(y) * tightRowStride;
    if (!needsBgraSwizzle) {
      std::memcpy(dstRow, srcRow, tightRowStride);
      continue;
    }
    for (u32 x = 0; x < copyWidth; ++x) {
      const auto* srcPixel = srcRow + static_cast<size_t>(x) * 4u;
      auto* dstPixel = dstRow + static_cast<size_t>(x) * 4u;
      dstPixel[0] = srcPixel[2];
      dstPixel[1] = srcPixel[1];
      dstPixel[2] = srcPixel[0];
      dstPixel[3] = srcPixel[3];
    }
  }
  readbackBuffer.Unmap();
  return true;
}
} // namespace aurora::gx

extern "C" {
GXRenderModeObj GXNtsc480IntDf = {
    VI_TVMODE_NTSC_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};
GXRenderModeObj GXNtsc480Int = {
    VI_TVMODE_NTSC_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {0, 0, 21, 22, 21, 0, 0},
};
GXRenderModeObj GXPal528IntDf = {
    VI_TVMODE_PAL_INT,
    704,
    528,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};
GXRenderModeObj GXMpal480IntDf = {
    VI_TVMODE_PAL_INT,
    640,
    480,
    480,
    40,
    0,
    640,
    480,
    VI_XFBMODE_DF,
    0,
    0,
    {6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6},
    {8, 8, 10, 12, 10, 8, 8},
};

void GXAdjustForOverscan(GXRenderModeObj* rmin, GXRenderModeObj* rmout, u16 hor, u16 ver) {
  *rmout = *rmin;
  const auto size = aurora::window::get_window_size();
  rmout->fbWidth = size.fb_width;
  rmout->efbHeight = size.fb_height;
  rmout->xfbHeight = size.fb_height;
}

void GXSetDispCopySrc(u16 left, u16 top, u16 wd, u16 ht) {
  g_gxState.dispCopy.src = {
      .x = left,
      .y = top,
      .width = wd,
      .height = ht,
  };
}

void GXSetTexCopySrc(u16 left, u16 top, u16 wd, u16 ht) { g_gxState.texCopySrc = {left, top, wd, ht}; }

void GXSetDispCopyDst(u16 wd, u16 ht) {
  g_gxState.dispCopy.dstWidth = wd;
  g_gxState.dispCopy.dstHeight = ht;
}

void GXSetTexCopyDst(u16 wd, u16 ht, GXTexFmt fmt, GXBool mipmap) {
  g_gxState.texCopyFmt = fmt;
  g_gxState.texCopyDstWidth = wd;
  g_gxState.texCopyDstHeight = ht;
}

void GXSetDispCopyFrame2Field(u32 mode) { g_gxState.dispCopy.frame2Field = mode; }

void GXSetCopyClamp(GXFBClamp clamp) { g_gxState.dispCopy.clamp = clamp; }

u32 GXSetDispCopyYScale(f32 vscale) {
  g_gxState.dispCopy.yScale = vscale;
  g_gxState.dispCopy.displayCopyYScale = display_copy_y_scale(vscale);
  return GXGetNumXfbLines(static_cast<u16>(std::max(g_gxState.dispCopy.src.height, 1)), vscale);
}

void GXSetCopyClear(GXColor color, u32 depth) {
  // BP 0x4F: clear color R + A
  u32 reg0 = 0;
  SET_REG_FIELD(0, reg0, 8, 0, color.r);
  SET_REG_FIELD(0, reg0, 8, 8, color.a);
  SET_REG_FIELD(0, reg0, 8, 24, 0x4F);
  GX_WRITE_RAS_REG(reg0);

  // BP 0x50: clear color B + G
  u32 reg1 = 0;
  SET_REG_FIELD(0, reg1, 8, 0, color.b);
  SET_REG_FIELD(0, reg1, 8, 8, color.g);
  SET_REG_FIELD(0, reg1, 8, 24, 0x50);
  GX_WRITE_RAS_REG(reg1);

  // BP 0x51: clear Z (24-bit)
  u32 reg2 = 0;
  SET_REG_FIELD(0, reg2, 24, 0, depth);
  SET_REG_FIELD(0, reg2, 8, 24, 0x51);
  GX_WRITE_RAS_REG(reg2);
  __gx->bpSent = 1;
}

void GXSetCopyFilter(GXBool aa, u8 sample_pattern[12][2], GXBool vf, u8 vfilter[7]) {}

void GXSetDispCopyGamma(GXGamma gamma) { g_gxState.dispCopy.gamma = gamma; }

void GXCopyDisp(void* dest, GXBool clear) {
  if (!gpu_copy_ready()) {
    return;
  }

  const auto rect = aurora::gx::map_logical_scissor(g_gxState.dispCopy.src);
  const auto [scaledWidth, scaledHeight] =
      scale_copy_dst(g_gxState.dispCopy.dstWidth, g_gxState.dispCopy.dstHeight);
  const auto dstWidth = std::max<u32>(scaledWidth, 1);
  const auto dstHeight = std::max<u32>(scaledHeight, 1);
  const aurora::gx::GXState::CopyTextureKey key{
      .dest = dest,
      .width = dstWidth,
      .height = dstHeight,
      .format = GX_TF_RGBA8,
  };
  auto it = g_gxState.copyTextureCache.find(key);
  if (it == g_gxState.copyTextureCache.end()) {
    auto handle = aurora::gfx::new_render_texture(dstWidth, dstHeight, GX_TF_RGBA8, "Display Copy Texture");
    it = g_gxState.copyTextureCache.emplace(key, aurora::gx::GXState::CopyTextureRef{.handle = handle, .revision = 0})
             .first;
  }
  auto& handle = it->second;

  const auto clearColor = clear && g_gxState.colorUpdate;
  const auto clearAlpha = clear && g_gxState.alphaUpdate;
  const auto clearDepth = clear && g_gxState.depthUpdate;
  aurora::gfx::resolve_pass(handle.handle, rect, clearColor, clearAlpha, clearDepth, g_gxState.clearColor,
                            aurora::gx::clear_depth_value(), GX_TF_RGBA8);
  ++handle.revision;
  g_gxState.frameDisplayCopyKey = key;
  g_gxState.frameDisplayCopyValid = true;
}

void GXCopyTex(void* dest, GXBool clear) {
  if (!gpu_copy_ready()) {
    return;
  }

  const auto rect = aurora::gx::map_logical_scissor(g_gxState.texCopySrc);
  const auto [dstWidth, dstHeight] = scale_copy_dst(g_gxState.texCopyDstWidth, g_gxState.texCopyDstHeight);
  const auto texCopyFmt = g_gxState.texCopyFmt;

  const aurora::gx::GXState::CopyTextureKey key{
      .dest = dest,
      .width = dstWidth,
      .height = dstHeight,
      .format = texCopyFmt,
  };
  auto it = g_gxState.copyTextureCache.find(key);
  if (it == g_gxState.copyTextureCache.end()) {
    aurora::gfx::TextureHandle handle;
    if (aurora::gfx::tex_copy_conv::needs_conversion(texCopyFmt)) {
      handle = aurora::gfx::new_conv_texture(dstWidth, dstHeight, texCopyFmt, "Copy Conv Texture");
    } else {
      // Configure the texture swizzle to use alpha 1.0 if targeting RGB565 or EFB doesn't have alpha
      const auto fmt = texCopyFmt == GX_TF_RGB565 || g_gxState.pixelFmt == GX_PF_RGB8_Z24 ||
                               g_gxState.pixelFmt == GX_PF_RGB565_Z16
                           ? GX_TF_RGB565
                           : GX_TF_RGBA8;
      handle = aurora::gfx::new_render_texture(dstWidth, dstHeight, fmt, "Resolved Texture");
    }
    it = g_gxState.copyTextureCache.emplace(key, aurora::gx::GXState::CopyTextureRef{.handle = handle, .revision = 0}).first;
  }
  auto& handle = it->second;

  if (g_gxState.alphaUpdate && g_gxState.dstAlpha != UINT32_MAX) {
    if (!clear) {
      // TODO: figure out the right behavior here.
      // should the copy have a specific alpha value but the EFB remains untouched?
    }
    // Overwrite alpha before resolving
    aurora::gfx::push_draw_command(aurora::gfx::clear::DrawData{
        .pipeline = aurora::gfx::pipeline_ref(aurora::gfx::clear::PipelineConfig{
            .clearColor = false,
            .clearAlpha = true,
            .clearDepth = false,
        }),
        .color = wgpu::Color{0.f, 0.f, 0.f, g_gxState.dstAlpha / 255.f},
    });
  }
  const auto clearColor = clear && g_gxState.colorUpdate;
  const auto clearAlpha = clear && g_gxState.alphaUpdate;
  const auto clearDepth = clear && g_gxState.depthUpdate;
  aurora::gfx::resolve_pass(handle.handle, rect, clearColor, clearAlpha, clearDepth, g_gxState.clearColor,
                            aurora::gx::clear_depth_value(), texCopyFmt);
  ++handle.revision;
  g_gxState.copyTextures[dest] = handle;
}

u16 GXGetNumXfbLines(u16 efbHeight, f32 yScale) {
  return static_cast<u16>(scaled_xfb_lines(efbHeight, display_copy_y_scale(yScale)));
}

f32 GXGetYScaleFactor(u16 efbHeight, u16 xfbHeight) {
  auto targetHeight = static_cast<u32>(std::max<u16>(xfbHeight, 1));
  auto yScale = static_cast<f32>(targetHeight) / static_cast<f32>(std::max<u16>(efbHeight, 1));
  auto realHeight = scaled_xfb_lines(efbHeight, display_copy_y_scale(yScale));

  while (realHeight > xfbHeight && targetHeight > 0u) {
    --targetHeight;
    yScale = static_cast<f32>(targetHeight) / static_cast<f32>(std::max<u16>(efbHeight, 1));
    realHeight = scaled_xfb_lines(efbHeight, display_copy_y_scale(yScale));
  }

  auto finalScale = yScale;
  while (realHeight < xfbHeight) {
    finalScale = yScale;
    ++targetHeight;
    yScale = static_cast<f32>(targetHeight) / static_cast<f32>(std::max<u16>(efbHeight, 1));
    realHeight = scaled_xfb_lines(efbHeight, display_copy_y_scale(yScale));
  }
  return finalScale;
}

// TODO GXClearBoundingBox
// TODO GXReadBoundingBox
}
