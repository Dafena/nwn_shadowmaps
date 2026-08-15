#pragma once

// Pure shadow-matrix helpers. Keep these independent of NWN and OpenGL state
// so matrix behaviour can be tested without loading the game.
#if defined(__GNUC__)
#include "shadow_config.h"   // SHADOW_HIDDEN
#define SHADOW_MATH_HIDDEN SHADOW_HIDDEN
#else
#define SHADOW_MATH_HIDDEN
#endif

SHADOW_MATH_HIDDEN void v_normalize(float v[3]);
SHADOW_MATH_HIDDEN void quat_from_to(const float a[3], const float b[3], float out[4]);
SHADOW_MATH_HIDDEN void mat4_mul(const float* a, const float* b, float* out);
SHADOW_MATH_HIDDEN void mat4_rigid_inverse(const float* m, float* out);
SHADOW_MATH_HIDDEN bool mat4_inverse(const float* m, float* out);
SHADOW_MATH_HIDDEN float mat4_max_delta(const float* a, const float* b);
SHADOW_MATH_HIDDEN void build_ortho(float e, float zn, float zf, float m[16]);
SHADOW_MATH_HIDDEN void build_perspective(float fovYRadians, float aspect,
                                          float zn, float zf, float m[16]);

#undef SHADOW_MATH_HIDDEN
