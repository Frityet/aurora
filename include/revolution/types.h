#pragma once

#include <dolphin/types.h>
#include <dolphin/mtx/GeoTypes.h>
#include <dolphin/os/OSTime.h>

#ifndef NO_INLINE
#if defined(__GNUC__) || defined(__clang__)
#define NO_INLINE __attribute__((noinline))
#else
#define NO_INLINE
#endif
#endif
