/**
 * xv6_math.c - Implementation of Core Mathematical Functions
 *
 * Contains both fixed-point (integer-only) and floating-point implementations.
 * The compilation is controlled by the USE_FIXED_POINT macro in xv6_math.h.
 *
 * OS Fall 2025 - Milestone 2
 */

// This is the primary header for all xv6 user-space programs.
// It provides access to system calls, standard library functions,
// and necessary type definitions like 'uint' and 'uint32'.
#include "user/user.h"
#include "user/xv6_math.h"
#include "kernel/types.h"
#include "user/xv6_math.h"


// =============================================================================
//  SECTION: FLOATING-POINT IMPLEMENTATION (FPU REQUIRED)
// =============================================================================
#ifndef USE_FIXED_POINT

/**
 * Floating-point absolute value.
 */
float xv6_fabsf(float x) {
    union {float f; uint i;} u = {x}; // Using 'uint' is standard in xv6
    u.i &= 0x7FFFFFFF;
    return u.f;
}

/**
 * Check for NaN (Not a Number).
 */
int xv6_isnan(float x) {
    union {float f; uint i;} u = {x};
    return (u.i & 0x7F800000) == 0x7F800000 && (u.i & 0x007FFFFF) != 0;
}

/**
 * Check for Infinity.
 */
int xv6_isinf(float x) {
    union {float f; uint i;} u = {x};
    return (u.i & 0x7FFFFFFF) == 0x7F800000;
}

/**
 * Floating-point square root using Newton-Raphson iteration.
 */
float xv6_sqrtf(float x) {
    if (x < 0) return NAN_F;
    if (x == 0 || xv6_isinf(x)) return x;

    union {float f; uint i;} u = {x};
    u.i = 0x5F3759DF - (u.i >> 1);
    float y = u.f;

    y = y * (1.5f - (x * 0.5f * y * y));
    y = y * (1.5f - (x * 0.5f * y * y));

    return 1.0f / y;
}

/**
 * Floating-point exponential (e^x).
 */
float xv6_expf(float x) {
    if (x > 88.7f) return INFINITY_F;
    if (x < -87.3f) return 0.0f;

    float k = (float)(int)(x / M_LN2);
    float r = x - k * M_LN2;

    float term = r;
    float sum = 1.0f + term;
    term *= r / 2.0f; sum += term;
    term *= r / 3.0f; sum += term;
    term *= r / 4.0f; sum += term;
    term *= r / 5.0f; sum += term;

    // * THIS IS THE FIX *
    // Changed 'int32' to 'uint', which is the standard 32-bit unsigned int in xv6 user space.
    union {float f; uint i;} u;
    u.i = (uint)((127.0f + k) * (1 << 23));

    return sum * u.f;
}


// Placeholder for logf, required by powf
float xv6_logf(float x) {
    return 0.0f;
}

/**
 * Floating-point power function.
 */
float xv6_powf(float base, float exp) {
    if (base < 0.0f) return NAN_F;
    return xv6_expf(exp * xv6_logf(base)); // Placeholder implementation
}

/**
 * Floating-point sine function.
 */
float xv6_sinf(float x) {
    while (x > M_2PI) x -= M_2PI;
    while (x < -M_2PI) x += M_2PI;

    float x2 = x*x;
    float term = x;
    float sum = term;
    term *= -x2 / (6.0f); sum += term;
    term *= -x2 / (20.0f); sum += term;
    return sum;
}

/**
 * Floating-point cosine function.
 */
float xv6_cosf(float x) {
    return xv6_sinf(x + M_PI_2);
}

#endif // NOT USE_FIXED_POINT