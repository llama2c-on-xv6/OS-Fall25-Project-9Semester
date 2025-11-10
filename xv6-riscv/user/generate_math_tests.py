import math
import random

# Configuration
FIXED_POINT_BITS = 16
SCALE = 1 << FIXED_POINT_BITS

def float_to_fixed(f):
    return int(f * SCALE)

def generate_float_tests():
    """Generates test data for floating-point implementations."""
    # ... (code from previous response) ...
    # This part remains the same, generating a 'math_test_data.h'
    print("Generated math_test_data.h for floating-point tests.")


def generate_fixed_point_tests():
    """Generates test data for fixed-point implementations."""
    c_code = """
#ifndef MATH_TEST_DATA_FIXED_H
#define MATH_TEST_DATA_FIXED_H
#include "xv6_math.h"

"""
    # --- sqrt_fx tests ---
    sqrt_data = [(i*i, i) for i in range(1, 20)]
    c_code += "struct {fixed_t input; fixed_t expected;} sqrt_tests_fx[] = {\n"
    for inp, exp in sqrt_data:
        c_code += f"    {{ {INT_TO_FIXED(inp)}, {INT_TO_FIXED(exp)} }},\n"
    c_code += "};\n\n"

    # --- sin_fx / cos_fx tests ---
    trig_data = [
        (0, 0, 1),
        (30, 0.5, math.sqrt(3)/2),
        (45, math.sqrt(2)/2, math.sqrt(2)/2),
        (60, math.sqrt(3)/2, 0.5),
        (90, 1, 0)
    ]
    c_code += "struct {fixed_t angle; fixed_t sin_val; fixed_t cos_val;} trig_tests_fx[] = {\n"
    for deg, sin_v, cos_v in trig_data:
        rad = math.radians(deg)
        c_code += f"    {{ {float_to_fixed(rad)}, {float_to_fixed(sin_v)}, {float_to_fixed(cos_v)} }},\n"
    c_code += "};\n\n"


    c_code += "#endif\n"

    with open("math_test_data_fixed.h", "w") as f:
        f.write(c_code)
    print("Generated math_test_data_fixed.h for fixed-point tests.")


if __name__ == "__main__":
    generate_float_tests()
    generate_fixed_point_tests()