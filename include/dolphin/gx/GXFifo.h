#ifndef DOLPHIN_GXFIFO_H
#define DOLPHIN_GXFIFO_H

#include <dolphin/gx/GXEnum.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  u8 pad[128];
} GXFifoObj;

typedef struct OSThread OSThread;
typedef void (*GXBreakPtCallback)(void);

void GXInitFifoBase(GXFifoObj* fifo, void* base, u32 size);
void GXInitFifoPtrs(GXFifoObj* fifo, void* readPtr, void* writePtr);
void GXGetFifoPtrs(const GXFifoObj* fifo, void** readPtr, void** writePtr);
OSThread *GXSetCurrentGXThread(void);
OSThread *GXGetCurrentGXThread(void);
GXBool GXGetCPUFifo(GXFifoObj* fifo);
GXBool GXGetGPFifo(GXFifoObj* fifo);
void GXEnableBreakPt(void* breakPt);
void GXDisableBreakPt(void);
GXBreakPtCallback GXSetBreakPtCallback(GXBreakPtCallback callback);
void GXSetCPUFifo(const GXFifoObj* fifo);
void GXSetGPFifo(const GXFifoObj* fifo);
void GXSaveCPUFifo(GXFifoObj* fifo);
void GXGetFifoStatus(GXFifoObj* fifo, GXBool* overhi, GXBool* underlow, u32* fifoCount, GXBool* cpu_write,
                     GXBool* gp_read, GXBool* fifowrap);
void GXGetGPStatus(GXBool* overhi, GXBool* underlow, GXBool* readIdle, GXBool* cmdIdle, GXBool* brkpt);
void GXInitFifoLimits(GXFifoObj* fifo, u32 hiWaterMark, u32 loWaterMark);
void* GXGetFifoBase(const GXFifoObj* fifo);
u32 GXGetFifoSize(const GXFifoObj* fifo);
u32 GXGetFifoCount(const GXFifoObj* fifo);
GXBool GXGetFifoWrap(const GXFifoObj* fifo);

#ifdef __cplusplus
}
#endif

#endif
