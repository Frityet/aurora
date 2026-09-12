#pragma once

#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

// Native write-gather pipe access. The original macros store these values to
// the GX MMIO FIFO; the host sends the same bytes to its command stream.
void GXWriteFifoU8(u8);
void GXWriteFifoU16(u16);
void GXWriteFifoU32(u32);
void GXWriteFifoF32(f32);

#define GX_WRITE_U8(value) GXWriteFifoU8((u8)(value))
#define GX_WRITE_U16(value) GXWriteFifoU16((u16)(value))
#define GX_WRITE_S16(value) GXWriteFifoU16((u16)(value))
#define GX_WRITE_U32(value) GXWriteFifoU32((u32)(value))
#define GX_WRITE_F32(value) GXWriteFifoF32((f32)(value))

#ifdef __cplusplus
}
#endif
