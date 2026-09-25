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
#include <string.h>

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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(array) ((s32)(sizeof(array) / sizeof((array)[0])))
#endif

// Compiler intrinsics declared by the original Revolution types header.
// Bit operations preserve NaN payloads, clear negative zero, and avoid signed
// overflow for the two's-complement INT_MIN absolute-value result.
#if !defined(__MWERKS__)
#ifdef __cplusplus
extern "C" {
#define RVL_INTRINSIC_NOEXCEPT noexcept
#else
#define RVL_INTRINSIC_NOEXCEPT
#endif
inline f32 __fabsf(f32 value) RVL_INTRINSIC_NOEXCEPT {
  u32 bits;
  memcpy(&bits, &value, sizeof(bits));
  bits &= 0x7fffffffU;
  memcpy(&value, &bits, sizeof(value));
  return value;
}
inline f64 __fabs(f64 value) RVL_INTRINSIC_NOEXCEPT {
  u64 bits;
  memcpy(&bits, &value, sizeof(bits));
  bits &= 0x7fffffffffffffffULL;
  memcpy(&value, &bits, sizeof(value));
  return value;
}
inline s32 __abs(s32 value) RVL_INTRINSIC_NOEXCEPT {
  u32 magnitude = value < 0 ? -(u32)value : (u32)value;
  memcpy(&value, &magnitude, sizeof(value));
  return value;
}
#undef RVL_INTRINSIC_NOEXCEPT
#ifdef __cplusplus
}
#endif
#endif
