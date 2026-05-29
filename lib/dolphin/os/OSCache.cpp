#include <dolphin/os/OSCache.h>

#include <cstring>

extern "C" void DCInvalidateRange(void*, u32) {
}

extern "C" void DCFlushRange(void*, u32) {
}

extern "C" void DCStoreRange(void*, u32) {
}

extern "C" void DCFlushRangeNoSync(void*, u32) {
}

extern "C" void DCStoreRangeNoSync(void*, u32) {
}

extern "C" void DCZeroRange(void* addr, u32 nBytes) {
    if (addr != nullptr && nBytes != 0) {
        std::memset(addr, 0, nBytes);
    }
}

extern "C" void DCTouchRange(void*, u32) {
}

extern "C" void ICInvalidateRange(void*, u32) {
}

extern "C" void LCEnable(void) {
}

extern "C" void LCDisable(void) {
}

extern "C" void LCLoadBlocks(void*, void*, u32) {
}

extern "C" void LCStoreBlocks(void*, void*, u32) {
}

extern "C" u32 LCLoadData(void*, void*, u32 nBytes) {
    return nBytes;
}

extern "C" u32 LCStoreData(void*, void*, u32 nBytes) {
    return nBytes;
}

extern "C" u32 LCQueueLength(void) {
    return 0;
}

extern "C" void LCQueueWait(u32) {
}

extern "C" void LCFlushQueue(void) {
}

extern "C" void __OSCacheInit(void) {
}
