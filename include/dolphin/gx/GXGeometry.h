#ifndef DOLPHIN_GXGEOMETRY_H
#define DOLPHIN_GXGEOMETRY_H

#include <dolphin/gx/GXEnum.h>

#ifdef __cplusplus
extern "C" {
#endif

void GXSetVtxDesc(GXAttr attr, GXAttrType type);
void GXSetVtxDescv(GXVtxDescList* list);
void GXClearVtxDesc(void);
void GXSetVtxAttrFmtv(GXVtxFmt vtxfmt, const GXVtxAttrFmtList* list);
void GXSetVtxAttrFmt(GXVtxFmt vtxfmt, GXAttr attr, GXCompCnt cnt, GXCompType type, u8 frac);
void GXSetNumTexGens(u8 nTexGens);
void GXBegin(GXPrimitive type, GXVtxFmt vtxfmt, u16 nverts);
#ifdef TARGET_PC
/**
 * Aurora extension: pass as GXBegin's vertex count to have it derived automatically from
 * the number of bytes written before GXEnd. Not supported while recording a display list.
 */
#define GX_AUTO 0xFFFF

/**
 * Aurora extension: begin an indexed triangle draw. The caller writes nverts vertices
 * then calls GXEnd. Indices are copied immediately on begin.
 */
void GXBeginIndexed(GXVtxFmt vtxfmt, u16 nverts, const u16* indices, u32 nindices);
#endif
void GXSetTexCoordGen2(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx, GXBool normalize,
                       u32 postmtx);
void GXSetLineWidth(u8 width, GXTexOffset texOffsets);
void GXSetPointSize(u8 pointSize, GXTexOffset texOffsets);
void GXEnableTexOffsets(GXTexCoordID coord, GXBool line_enable, GXBool point_enable);
void GXSetArray(GXAttr attr, const void* data, u8 stride);
#ifdef TARGET_PC
void GXSetArraySized(GXAttr attr, const void* data, u32 size, u8 stride, bool le);
// Native counterpart of a CP array-base write: preserves the current stride.
// The unsized, host-endian source remains borrowed until the FIFO consumes it.
void GXSetArrayBase(GXAttr attr, const void* data);
#endif
#ifdef TARGET_PC
#define GXSETARRAY(attr, data, size, stride, le) GXSetArraySized((attr), (data), (size), (stride), (le))
#else
#define GXSETARRAY(attr, data, size, stride, le) GXSetArray((attr), (data), (stride))
#endif
void GXInvalidateVtxCache(void);

static inline void GXSetTexCoordGen(GXTexCoordID dst_coord, GXTexGenType func, GXTexGenSrc src_param, u32 mtx) {
  GXSetTexCoordGen2(dst_coord, func, src_param, mtx, GX_FALSE, GX_PTIDENTITY);
}

#ifdef __cplusplus
}
#endif

#endif
