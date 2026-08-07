#pragma once

#include <revolution/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MEMAllocator MEMAllocator;
typedef void* (*MEMFuncAllocatorAlloc)(MEMAllocator* allocator, u32 size);
typedef void (*MEMFuncAllocatorFree)(MEMAllocator* allocator, void* memory);

typedef struct MEMAllocatorFunc {
  MEMFuncAllocatorAlloc pfAlloc;
  MEMFuncAllocatorFree pfFree;
} MEMAllocatorFunc;

struct MEMAllocator {
  const MEMAllocatorFunc* pFunc;
  void* pHeap;
  u32 heapParam1;
  u32 heapParam2;
};

static inline void* MEMAllocFromAllocator(MEMAllocator* allocator, u32 size) {
  return allocator->pFunc->pfAlloc(allocator, size);
}

static inline void MEMFreeToAllocator(MEMAllocator* allocator, void* memory) {
  allocator->pFunc->pfFree(allocator, memory);
}

#ifdef __cplusplus
}
#endif
