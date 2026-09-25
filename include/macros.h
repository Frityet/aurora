#pragma once

// Original RVL angle conversion constants and operation order.
#ifndef M_PI_F
#define M_PI_F 3.1415926f
#endif
#ifndef M_TAU
#define M_TAU 6.283185307179586
#endif
#ifndef DEG_TO_RAD_MULT_CONSTANT
#define DEG_TO_RAD_MULT_CONSTANT (M_PI_F / 180.0f)
#endif
#ifndef RAD_TO_DEG_MULT_CONSTANT
#define RAD_TO_DEG_MULT_CONSTANT (180.0f / M_PI_F)
#endif
#ifndef DEG_TO_RAD
#define DEG_TO_RAD(x) ((x) * DEG_TO_RAD_MULT_CONSTANT)
#endif
#ifndef RAD_TO_DEG
#define RAD_TO_DEG(x) ((x) * RAD_TO_DEG_MULT_CONSTANT)
#endif
