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
  return allocator != nullptr && allocator->pFunc != nullptr && allocator->pFunc->pfAlloc != nullptr
             ? allocator->pFunc->pfAlloc(allocator, size)
             : nullptr;
}

static inline void MEMFreeToAllocator(MEMAllocator* allocator, void* memory) {
  if (allocator != nullptr && allocator->pFunc != nullptr && allocator->pFunc->pfFree != nullptr) {
    allocator->pFunc->pfFree(allocator, memory);
  }
}

#ifdef __cplusplus
}
#endif
