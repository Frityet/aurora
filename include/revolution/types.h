#pragma once

#ifndef ALIGN_PREV
#define ALIGN_PREV(X, N) ((X) & ~((N) - 1))
#endif
#ifndef ALIGN_NEXT
#define ALIGN_NEXT(X, N) ALIGN_PREV(((X) + (N) - 1), N)
#endif

#include <dolphin/types.h>
#include <dolphin/mtx/GeoTypes.h>
#include <dolphin/os/OSTime.h>
#include <stdint.h>

#ifndef NO_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif
#endif

#ifndef ROUND_UP
#define ROUND_UP(value, alignment) (((value) + (alignment) - 1) & ~((alignment) - 1))
#endif
#ifndef ROUND_UP_PTR
#define ROUND_UP_PTR(value, alignment) ((void*)(((uintptr_t)(value) + (alignment) - 1) & ~((uintptr_t)(alignment) - 1)))
#endif

#ifndef ARRAY_SIZEU
#define ARRAY_SIZEU(array) (sizeof(array) / sizeof((array)[0]))
#endif
#ifndef IS_ALIGNED
#define IS_ALIGNED(value, alignment) (((uintptr_t)(value) & ((alignment) - 1)) == 0)
#endif
#ifndef IS_NOT_ALIGNED
#define IS_NOT_ALIGNED(value, alignment) (((uintptr_t)(value) & ((alignment) - 1)) != 0)
#endif
