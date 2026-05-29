#include <dolphin/vi.h>

#include "../../window.hpp"
#include "aurora/math.hpp"
#include "vi_internal.hpp"

#include <optional>

namespace aurora::vi {
std::optional<GXRenderModeObj> g_renderMode;
u32 g_retraceCount = 0;
void* g_nextFrameBuffer = nullptr;
void* g_currentFrameBuffer = nullptr;
BOOL g_black = FALSE;
BOOL g_dimmingEnabled = TRUE;
VIRetraceCallback g_preRetraceCallback = nullptr;
VIRetraceCallback g_postRetraceCallback = nullptr;

Vec2<uint32_t> render_mode_size() noexcept {
  if (!g_renderMode) {
    return {640, 480};
  }
  return {g_renderMode->fbWidth, g_renderMode->efbHeight};
}

void configure(const GXRenderModeObj* rm) noexcept {
  const auto oldSize = render_mode_size();
  if (rm == nullptr) {
    g_renderMode.reset();
  } else {
    g_renderMode = *rm;
  }
  if (render_mode_size() != oldSize) {
    window::request_frame_buffer_resize();
  }
}

Vec2<uint32_t> configured_fb_size() noexcept {
  return render_mode_size();
}
} // namespace aurora::vi

extern "C" {
void VIInit() {
  aurora::vi::g_retraceCount = 0;
  aurora::vi::g_nextFrameBuffer = nullptr;
  aurora::vi::g_currentFrameBuffer = nullptr;
  aurora::vi::g_black = FALSE;
  aurora::vi::g_dimmingEnabled = TRUE;
}
void VIConfigure(const GXRenderModeObj* rm) { aurora::vi::configure(rm); }
void VIConfigurePan(u16 xOrg, u16 yOrg, u16 width, u16 height) {
  if (!aurora::vi::g_renderMode) {
    aurora::vi::g_renderMode = GXRenderModeObj{};
  }
  aurora::vi::g_renderMode->viXOrigin = xOrg;
  aurora::vi::g_renderMode->viYOrigin = yOrg;
  aurora::vi::g_renderMode->viWidth = width;
  aurora::vi::g_renderMode->viHeight = height;
}
u32 VIGetTvFormat() {
  if (!aurora::vi::g_renderMode) {
    return VI_NTSC;
  }
  return static_cast<u32>(aurora::vi::g_renderMode->viTVmode) >> 2U;
}
u32 VIGetScanMode() {
  if (!aurora::vi::g_renderMode) {
    return VI_INTERLACE;
  }
  return static_cast<u32>(aurora::vi::g_renderMode->viTVmode) & 0x3U;
}
u32 VIGetCurrentLine() {
  const auto size = aurora::vi::render_mode_size();
  const auto height = size.y == 0 ? 1U : size.y;
  return aurora::vi::g_retraceCount % height;
}
u32 VIGetRetraceCount() { return aurora::vi::g_retraceCount; }
u32 VIGetNextField() { return aurora::vi::g_retraceCount & 1U; }
u32 VIGetDTVStatus() { return VIGetScanMode() == VI_PROGRESSIVE ? 1U : 0U; }
void VIWaitForRetrace() {
  if (aurora::vi::g_preRetraceCallback != nullptr) {
    aurora::vi::g_preRetraceCallback(aurora::vi::g_retraceCount);
  }
  ++aurora::vi::g_retraceCount;
  aurora::vi::g_currentFrameBuffer = aurora::vi::g_nextFrameBuffer;
  if (aurora::vi::g_postRetraceCallback != nullptr) {
    aurora::vi::g_postRetraceCallback(aurora::vi::g_retraceCount);
  }
}
void VIFlush() {}
void* VIGetCurrentFrameBuffer() { return aurora::vi::g_currentFrameBuffer; }
void* VIGetNextFrameBuffer() { return aurora::vi::g_nextFrameBuffer; }
void VISetNextFrameBuffer(void* fb) { aurora::vi::g_nextFrameBuffer = fb; }
void VISetBlack(BOOL black) { aurora::vi::g_black = black != FALSE ? TRUE : FALSE; }
BOOL VIEnableDimming(BOOL enabled) {
  const auto previous = aurora::vi::g_dimmingEnabled;
  aurora::vi::g_dimmingEnabled = enabled != FALSE ? TRUE : FALSE;
  return previous;
}
BOOL VIResetDimmingCount() { return TRUE; }
VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
  const auto previous = aurora::vi::g_preRetraceCallback;
  aurora::vi::g_preRetraceCallback = cb;
  return previous;
}
VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
  const auto previous = aurora::vi::g_postRetraceCallback;
  aurora::vi::g_postRetraceCallback = cb;
  return previous;
}

void VISetWindowTitle(const char* title) { aurora::window::set_title(title); }
void VISetWindowFullscreen(bool fullscreen) { aurora::window::set_fullscreen(fullscreen); }
bool VIGetWindowFullscreen() { return aurora::window::get_fullscreen(); }
void VISetWindowSize(uint32_t width, uint32_t height) { aurora::window::set_window_size(width, height); }
void VISetWindowPosition(uint32_t x, uint32_t y) { aurora::window::set_window_position(x, y); }
void VICenterWindow() { aurora::window::center_window(); }
void VISetFrameBufferScale(float scale) { aurora::window::set_frame_buffer_scale(scale); }
}
