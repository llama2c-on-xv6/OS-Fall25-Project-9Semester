/**

 * xv6_math.h - Core Mathematical Functions for xv6

 *

 * Provides implementations for essential mathematical functions required for

 * neural network inference. It supports two modes via the USE_FIXED_POINT macro:

 *

 * 1. Fixed-Point Mode (USE_FIXED_POINT is defined)

 * 2. Floating-Point Mode (USE_FIXED_POINT is not defined)

 *

 * OS Fall 2025 - Milestone 2

 */



#ifndef XV6_MATH_H

#define XV6_MATH_H

#define INFINITY_F (__builtin_inff())
#define NAN_F      (__builtin_nanf(""))


// NOTE: All type definitions (like uint32) are now handled by including

// "user/user.h" in the .c file. This header is now clean.



// =============================================================================

//  Compilation Mode Switch

// =============================================================================

// Comment out the following line to switch to Floating-Point mode

// #define USE_FIXED_POINT



// =============================================================================

//  Public API (Interface)

// =============================================================================

float xv6_sqrtf(float x);

float xv6_expf(float x);

float xv6_powf(float base, float exponent);

float xv6_sinf(float x);

float xv6_cosf(float x);

float xv6_fabsf(float x);



// Helper functions for special floating-point values

int xv6_isnan(float x);

int xv6_isinf(float x);



#ifndef USE_FIXED_POINT

#define M_PI 3.1415926535f

#define M_PI_2 1.5707963267f

#define M_2PI 6.2831853071f

#define M_E  2.7182818284f

#define M_LN2 0.6931471805f



// #define INFINITY_F (__built-in_inff())

// #define NAN_F      (__built-in_nanf(""))

#endif



#endif /* XV6_MATH_H */