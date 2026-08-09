#ifndef DOLPHIN_GXAURORA_H
#define DOLPHIN_GXAURORA_H

#include <dolphin/types.h>

#if __cplusplus
extern "C" {
#endif

//
// Subcommands for GX_AURORA.
//

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Must be followed by six f32 values: left, top, width, height, nearz, farz.
 */
#define GX_AURORA_LOAD_VIEWPORT_RENDER 0x0001

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Must be followed by four u32 values: left, top, width, height.
 */
#define GX_AURORA_LOAD_SCISSOR_RENDER 0x0002

/**
 * Loads a full 4x4 projection matrix, bypassing GXSetProjection's 6-parameter
 * hardware encoding. Must be followed by sixteen f32 values in row-major order.
 */
#define GX_AURORA_LOAD_PROJECTION_FULL 0x0003

/**
 * Aurora equivalent of CP_REG_ARRAYBASE_ID: sets the base address and size of a vertex array.
 * This command must be followed by a 64-bit memory address, 32-bit size, and 1-byte flags field.
 * Bit 0 of the flags field marks little-endian array data. Bit 1 marks the retail unsized form: its
 * required source span is derived from indexed FIFO draws and indexed XF loads instead of being guessed.
 * The index of the vertex array is given by the lowest 4 bits of the command ID,
 * e.g. writing GX_AURORA_LOAD_ARRAYBASE + 5 will set the vertex array for the sixth vertex attribute.
 * To set strides, use the normal CP_REG_ARRAYSTRIDE_ID register.
 */
#define GX_AURORA_LOAD_ARRAYBASE 0x0010

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
#define GX_AURORA_DEBUG_GROUP_PUSH 0x0020

/**
 * Pops a previously pushed debug group.
 * Followed by nothing.
 */
#define GX_AURORA_DEBUG_GROUP_POP 0x0021

/**
 * Sends a debug marker to the backend graphics API.
 * Must be followed by a u16 string length and that many UTF-8 characters (no null terminator required).
 */
#define GX_AURORA_DEBUG_MARKER_INSERT 0x0022

#define GX_AURORA_LOAD_TEXOBJ 0x0030

#define GX_AURORA_LOAD_TLUT 0x0031

#define GX_AURORA_DESTROY_TEXOBJ 0x0032

#define GX_AURORA_DESTROY_TLUT 0x0033

#define GX_AURORA_DESTROY_COPY_TEX 0x0034

#define GX_AURORA_LOAD_COPY_SRC 0x0035

#define GX_AURORA_LOAD_COPY_DST 0x0036

#define GX_AURORA_LOAD_COPY_DEST 0x0037

#define GX_AURORA_REQUEST_DEPTH_SNAPSHOT 0x0038

#define GX_AURORA_BEGIN_OFFSCREEN 0x0039

#define GX_AURORA_END_OFFSCREEN 0x003A

/**
 * Captures the depth buffer at this exact point in the GX FIFO stream.
 * Must be followed by an AuroraDepthSnapshotId.
 */
#define GX_AURORA_REQUEST_TAGGED_DEPTH_SNAPSHOT 0x003B

/**
 * Loads the effective GX display-copy sample pattern and vertical filter in FIFO order.
 * Followed by aa/vfilter enable bytes, 24 sample-position bytes, and seven filter coefficients.
 */
#define GX_AURORA_LOAD_COPY_FILTER 0x003C

/**
 * Draw primitives with the vertex count derived from a byte length, as written by
 * GXBegin(prim, fmt, GX_AUTO). Must be followed by a u8 draw opcode (vtxfmt|prim),
 * a u32 vertex data byte length, then that many bytes of vertex data. The byte length
 * must be a whole multiple of the current vertex size or zero (no draw).
 */
#define GX_AURORA_DRAW_SIZED 0x0040

/**
 * Draw pre-merged triangles with a prebuilt index buffer, as written by the display
 * list optimizer (aurora::gx::dl::optimize). Must be followed by a u8 draw opcode
 * (vtxfmt | GX_TRIANGLES), a u16 vertex count, a u32 index count, that many u16
 * indices, then vertex count * vertex size bytes of packed vertex data. Index data
 * is always host-endian regardless of stream endianness.
 */
#define GX_AURORA_DRAW_INDEXED 0x0041

#define GX2_SET_POLYGON_OFFSET 0x1000


/*
 * Debug marker stuff
 */

/**
 * Pushes a debug group to the backend graphics API. These may show in debugging tools such as RenderDoc.
 * It is considered an error to have unpopped debug groups at the end of the frame. They will be automatically cleared.
 */
void GXPushDebugGroup(const char* label);

/**
 * Pop a debug group previously pushed via GXPushDebugGroup().
 */
void GXPopDebugGroup();

/**
 * Sends a debug marker to the backend graphics API. These may show in debugging tools such as RenderDoc.
 */
void GXInsertDebugMarker(const char* label);

typedef enum _AuroraViewportPolicy {
  AURORA_VIEWPORT_FIT = 0,     // Preserve logical aspect in the content framebuffer
  AURORA_VIEWPORT_STRETCH = 1, // Match content framebuffer aspect to the native surface
  AURORA_VIEWPORT_NATIVE = 2,  // Use active framebuffer pixels directly
} AuroraViewportPolicy;

typedef u64 AuroraDepthSnapshotId;

#define AURORA_INVALID_DEPTH_SNAPSHOT_ID ((AuroraDepthSnapshotId)0)

typedef enum _AuroraDepthSnapshotStatus {
  AURORA_DEPTH_SNAPSHOT_UNKNOWN = 0,
  AURORA_DEPTH_SNAPSHOT_PENDING = 1,
  AURORA_DEPTH_SNAPSHOT_READY = 2,
  AURORA_DEPTH_SNAPSHOT_DROPPED = 3,
} AuroraDepthSnapshotStatus;

typedef struct _AuroraDepthSnapshotInfo {
  AuroraDepthSnapshotId id;
  u64 frameId;
  u32 width;
  u32 height;
  f32 viewportNear;
  f32 viewportFar;
} AuroraDepthSnapshotInfo;

/**
 * Requests an asynchronous depth snapshot at this exact point in the GX FIFO.
 * The returned ID remains queryable until released or expired by the bounded
 * snapshot store. Zero is never returned for a valid request.
 */
AuroraDepthSnapshotId GXAuroraRequestDepthSnapshot(void);

/**
 * Returns the request status and, when known, its immutable frame/viewport
 * metadata. The snapshot pixel grid is width by height.
 */
AuroraDepthSnapshotStatus GXAuroraGetDepthSnapshotInfo(AuroraDepthSnapshotId id, AuroraDepthSnapshotInfo* info);

/**
 * Reads one GX Z24 value from a completed snapshot. Returns FALSE unless the
 * exact requested ID is ready and the coordinates are in bounds.
 */
BOOL GXAuroraReadDepthSnapshotZ(AuroraDepthSnapshotId id, u16 x, u16 y, u32* z);

/**
 * Releases a tagged snapshot. Subsequent queries for the ID return UNKNOWN.
 */
void GXAuroraReleaseDepthSnapshot(AuroraDepthSnapshotId id);

/**
 * Configures content framebuffer sizing and how GXSetViewport/GXSetScissor parameters are applied to rendering.
 * When AURORA_VIEWPORT_NATIVE is used, GXSetTexCopySrc/GXSetTexCopyDst will use native framebuffer resolution.
 */
void AuroraSetViewportPolicy(AuroraViewportPolicy policy);

/**
 * Retrieves the current content framebuffer size.
 */
void AuroraGetRenderSize(u32* width, u32* height);

/**
 * Returns whether GX commands can currently be submitted to an active frame.
 */
BOOL AuroraIsFrameActive(void);

/**
 * Returns whether GXCopyTex has materialized a GPU texture for the destination address.
 */
BOOL AuroraHasTextureCopy(const void* dest);

/**
 * Returns whether the current/most recent frame produced a GXCopyDisp display copy.
 */
BOOL AuroraHasDisplayCopy(void);

/**
 * Returns the size, in pixels, of the current/most recent GXCopyDisp display copy.
 */
BOOL AuroraGetDisplayCopySize(u32* width, u32* height);

/**
 * Reads the current/most recent GXCopyDisp display copy into RGBA8 pixels.
 * This is intended for screenshots and parity readback after aurora_end_frame().
 * rowStrideOut receives the number of bytes per row written to dst.
 */
BOOL AuroraReadDisplayCopyRGBA8(void* dst, u32 dstSize, u32* width, u32* height, u32* rowStrideOut);

/**
 * Sets the actual render viewport in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetViewport.
 */
void GXSetViewportRender(f32 left, f32 top, f32 wd, f32 ht, f32 nearz, f32 farz);

/**
 * Sets the actual render scissor in native framebuffer coordinates.
 * Overrides the automatically scaled values set by the logical GXSetScissor.
 */
void GXSetScissorRender(u32 left, u32 top, u32 wd, u32 ht);

void GX2SetPolygonOffset(f32 mFrontOffset, f32 mFrontScale, f32 mBackOffset, f32 mBackScale, f32 mClamp);

/**
 * Load an arbitrary 4x4 projection matrix, avoiding the 6-parameter hardware encoding.
 */
void GXSetProjectionFull(const void* mtx);

/**
 * Create an offscreen framebuffer and switch rendering to it.
 * All subsequent GX rendering will target this framebuffer until GXRestoreFrameBuffer() is called.
 * Use GXCopyTex to resolve the offscreen content into a texture.
 */
void GXCreateFrameBuffer(u32 width, u32 height);

/**
 * Restore rendering to the main EFB framebuffer.
 * Must be called after GXCreateFrameBuffer() to resume normal rendering.
 */
void GXRestoreFrameBuffer(void);

#if __cplusplus
}
#endif

#endif
