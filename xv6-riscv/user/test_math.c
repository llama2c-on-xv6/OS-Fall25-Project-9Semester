/**
 * test_math.c - Unit Test Runner for the xv6 Math Library
 *
 * This file contains the main test runner. It automatically switches between
 * testing the fixed-point or floating-point implementations based on the
 * USE_FIXED_POINT macro defined in xv6_math.h.
 *
 * OS Fall 2025 - Milestone 2
 */
#include "kernel/types.h"
#include "user/user.h"
#include "xv6_math.h"

// The preprocessor will include the correct test data file.
#ifdef USE_FIXED_POINT
#include "math_test_data_fixed.h"
#else
// Assume generate_math_tests.py was run to create this
// #include "math_test_data.h"
#endif

void run_fixed_point_tests() {
    printf("--- Running Fixed-Point Math Tests ---\n");

    // Test sqrt_fx
    int sqrt_passed = 0;
    int sqrt_total = sizeof(sqrt_tests_fx) / sizeof(sqrt_tests_fx[0]);
    for (int i = 0; i < sqrt_total; i++) {
        fixed_t result = xv6_sqrt_fx(sqrt_tests_fx[i].input);
        // Allow a small error margin for fixed point calculations
        if (xv6_fabs_fx(result - sqrt_tests_fx[i].expected) < 100) {
            sqrt_passed++;
        } else {
            printf("  FAIL: sqrt_fx(%d) -> got %d, want %d\n",
                   sqrt_tests_fx[i].input, result, sqrt_tests_fx[i].expected);
        }
    }
    printf("sqrt_fx: %d / %d passed.\n", sqrt_passed, sqrt_total);

    // Add more fixed-point tests for sin, cos, etc. here
}

void run_floating_point_tests() {
    printf("--- Running Floating-Point Math Tests ---\n");
    printf("NOTE: Floating-point test suite is extensive and requires\n");
    printf("      the full 'math_test_data.h' file.\n");
    printf("      This is a placeholder for the full test runner.\n");

    // A simple smoke test for floating point
    float val = 4.0f;
    float result = xv6_sqrtf(val);
    if (xv6_fabsf(result - 2.0f) < 0.0001f) {
        printf("sqrtf(4.0): PASS\n");
    } else {
        // You can't print floats in standard xv6, so we print integer parts.
        printf("sqrtf(4.0): FAIL -> got %d\n", (int)result);
    }
}

int main(int argc, char *argv[]) {
    printf("========================================\n");
    printf("   XV6 Math Library Test Suite\n");
    printf("========================================\n");

#ifdef USE_FIXED_POINT
    printf("Mode: Fixed-Point (Integer-Only Arithmetic)\n\n");
    run_fixed_point_tests();
#else
    printf("Mode: Floating-Point (FPU Required)\n\n");
    run_floating_point_tests();
#endif

    printf("\n--- Test Suite Complete ---\n");
    exit(0);
}