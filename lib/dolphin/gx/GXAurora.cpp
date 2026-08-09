#include "dolphin/gx/GXAurora.h"

#include <limits>

#include "__gx.h"
#include "gx.hpp"
#include "../../window.hpp"

#include "../../gfx/common.hpp"
#include "../../gfx/depth_peek.hpp"
#include "../../gx/fifo.hpp"

static void GXWriteString(const char* label) {
  auto length = strlen(label);

  if (length > std::numeric_limits<u16>::max()) {
    Log.warn("Debug marker size over u16 max, truncating");
    length = std::numeric_limits<u16>::max();
  }

  GX_WRITE_U16(length);
  GX_WRITE_DATA(label, length);
}

void GXPushDebugGroup(const char* label) {
  GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_PUSH);
  GXWriteString(label);
}

void GXPopDebugGroup() { GX_WRITE_AURORA(GX_AURORA_DEBUG_GROUP_POP); }

void GXInsertDebugMarker(const char* label) {
  GX_WRITE_AURORA(GX_AURORA_DEBUG_MARKER_INSERT);
  GXWriteString(label);
}

void AuroraSetViewportPolicy(AuroraViewportPolicy policy) {
  g_gxState.viewportPolicy = policy;
  aurora::window::set_frame_buffer_aspect_fit(policy == AURORA_VIEWPORT_FIT);
}

void AuroraGetRenderSize(u32* width, u32* height) {
  const auto windowSize = aurora::window::get_window_size();
  if (width != nullptr) {
    *width = windowSize.fb_width;
  }
  if (height != nullptr) {
    *height = windowSize.fb_height;
  }
}

AuroraDepthSnapshotId GXAuroraRequestDepthSnapshot(void) {
  if (aurora::gx::fifo::in_display_list()) {
    Log.warn("GXAuroraRequestDepthSnapshot cannot be recorded in a display list");
    return AURORA_INVALID_DEPTH_SNAPSHOT_ID;
  }

  const auto id = aurora::gfx::depth_peek::create_snapshot();
  GX_WRITE_AURORA(GX_AURORA_REQUEST_TAGGED_DEPTH_SNAPSHOT);
  GX_WRITE_U64(id);
  return id;
}

AuroraDepthSnapshotStatus GXAuroraGetDepthSnapshotInfo(AuroraDepthSnapshotId id, AuroraDepthSnapshotInfo* info) {
  return aurora::gfx::depth_peek::get_snapshot_info(id, info);
}

BOOL GXAuroraReadDepthSnapshotZ(AuroraDepthSnapshotId id, u16 x, u16 y, u32* z) {
  if (z == nullptr) {
    return FALSE;
  }
  uint32_t value = 0;
  if (!aurora::gfx::depth_peek::read_snapshot(id, x, y, value)) {
    return FALSE;
  }
  *z = value;
  return TRUE;
}

void GXAuroraReleaseDepthSnapshot(AuroraDepthSnapshotId id) { aurora::gfx::depth_peek::release_snapshot(id); }

BOOL AuroraIsFrameActive(void) { return aurora::gfx::is_frame_active() ? TRUE : FALSE; }

BOOL AuroraHasTextureCopy(const void* dest) {
  if (aurora::gfx::is_frame_active()) {
    GXFlush();
    aurora::gx::fifo::drain();
  }
  return aurora::gx::has_copy_texture(dest) ? TRUE : FALSE;
}

BOOL AuroraHasDisplayCopy(void) { return aurora::gx::has_display_copy() ? TRUE : FALSE; }

BOOL AuroraGetDisplayCopySize(u32* width, u32* height) {
  return aurora::gx::display_copy_size(width, height) ? TRUE : FALSE;
}

BOOL AuroraReadDisplayCopyRGBA8(void* dst, u32 dstSize, u32* width, u32* height, u32* rowStrideOut) {
  return aurora::gx::read_display_copy_rgba8(dst, dstSize, width, height, rowStrideOut) ? TRUE : FALSE;
}

void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_VIEWPORT_RENDER);
  GX_WRITE_F32(left);
  GX_WRITE_F32(top);
  GX_WRITE_F32(wd);
  GX_WRITE_F32(ht);
  GX_WRITE_F32(nearz);
  GX_WRITE_F32(farz);
}

void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht) {
  GX_WRITE_AURORA(GX_AURORA_LOAD_SCISSOR_RENDER);
  GX_WRITE_U32(left);
  GX_WRITE_U32(top);
  GX_WRITE_U32(wd);
  GX_WRITE_U32(ht);
}

void GXSetProjectionFull(const void* mtx) {
  const f32* values = reinterpret_cast<const f32*>(mtx);
  GX_WRITE_AURORA(GX_AURORA_LOAD_PROJECTION_FULL);
  for (int i = 0; i < 16; ++i) {
    GX_WRITE_F32(values[i]);
  }
}

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp) {
  GX_WRITE_AURORA(GX2_SET_POLYGON_OFFSET);
  GX_WRITE_F32(mFrontOffset);
  GX_WRITE_F32(mFrontScale);
  GX_WRITE_F32(mBackOffset);
  GX_WRITE_F32(mBackScale);
  GX_WRITE_F32(mClamp);
}

void GXCreateFrameBuffer(u32 width, u32 height) {
  GX_WRITE_AURORA(GX_AURORA_BEGIN_OFFSCREEN);
  GX_WRITE_U32(width);
  GX_WRITE_U32(height);
}

void GXRestoreFrameBuffer() {
  GX_WRITE_AURORA(GX_AURORA_END_OFFSCREEN);
}
