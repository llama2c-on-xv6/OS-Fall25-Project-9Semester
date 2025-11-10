// user/test_stdlib.c
// Comprehensive test suite for the custom xv6 Standard Library (stdlib.c)

#include "kernel/types.h"
#include "user/user.h"
#include "stdlib.h" // Your new stdlib.h

// Tolerance for comparing floating-point numbers
#define EPSILON 0.0000001

// --- Test Reporting Helpers ---

/**
 * @brief Prints the status of a single test case.
 */
void print_test_status(char *test_name, int success) {
    if (success) {
        printf("TEST SUCCESS: %s\n", test_name);
    } else {
        printf("TEST FAILED: %s\n", test_name);
    }
}

// --- Comparators (Crucial for QSort/BSearch with LLM data) ---

/**
 * @brief Comparator for integer types (int).
 */
int compare_ints(const void *a, const void *b) {
    int val_a = *(const int *)a;
    int val_b = *(const int *)b;
    
    if (val_a < val_b) return -1;
    if (val_a > val_b) return 1;
    return 0;
}

/**
 * @brief Comparator for double types (LLM Weights).
 */
int compare_doubles(const void *a, const void *b) {
    double val_a = *(const double *)a;
    double val_b = *(const double *)b;
    
    // Check for near equality due to floating point nature
    if (val_a - val_b > EPSILON) return 1;
    if (val_b - val_a > EPSILON) return -1;
    return 0;
}


// =========================================================================
//                               TEST FUNCTIONS
// =========================================================================


/*
 * calloc Test Cases (Memory Allocation and Zeroing)
 * Tests robustness (overflow, zero-size handling).
 */
void test_calloc() {
    printf("\n--- Testing calloc ---\n");
    int success = 1;

    // 1. Basic Allocation and Zeroing (10 ints)
    uint num_elements = 10;
    int *arr = (int*)calloc(num_elements, sizeof(int));
    if (arr == 0) {
        print_test_status("Calloc Basic Allocation (Pointer Check)", 0);
        return;
    }
    for (uint i = 0; i < num_elements; i++) {
        if (arr[i] != 0) {
            success = 0;
            break;
        }
    }
    print_test_status("Calloc Basic Zeroing (10 ints)", success);
    free(arr);

    // 2. Edge Case: Zero elements (Must return a non-NULL pointer compatible with free())
    int *arr_zero = (int*)calloc(0, sizeof(int));
    if (arr_zero != 0) {
        print_test_status("Calloc Zero Elements (Valid for free())", 1);
        free(arr_zero);
    } else {
        // If your calloc returned NULL, which is also standard-compliant, report that.
        print_test_status("Calloc Zero Elements (Returned NULL)", 1);
    }
    
    // 3. Edge Case: Check for Overflow (Should return NULL)
    // Assuming uint is 32-bit, this overflows: 2^32 / 8 + 1
    // Note: The specific values might need adjustment based on xv6 uint size.
    uint large_n = 0xFFFFFFF0; // A value close to 2^32
    uint large_size = 10;
    void *arr_overflow = calloc(large_n, large_size);
    print_test_status("Calloc Integer Overflow Check", arr_overflow == 0);
    if (arr_overflow != 0) free(arr_overflow); // Clean up if check failed
}


/*
 * atoi Test Cases (ASCII to Integer)
 */
void test_atoi() {
    printf("\n--- Testing atoi ---\n");

    print_test_status("Atoi Positive (123)", atoi("123") == 123);
    print_test_status("Atoi Negative (-45)", atoi("-45") == -45);
    print_test_status("Atoi Leading Whitespace", atoi("  \t-10") == -10);
    print_test_status("Atoi Mixed (Sign, Suffix)", atoi("+67abc") == 67);
}


/*
 * atof Test Cases (ASCII to Double/Float)
 * CRITICAL for testing soft-float linkage.
 */
void test_atof() {
    printf("\n--- Testing atof (Soft-Float Test) ---\n");

    // 1. Simple decimals
    print_test_status("Atof Basic Decimal (12.34)", 
        (atof("12.34") - 12.34) < EPSILON);
    print_test_status("Atof Negative (-0.5)", 
        (atof("-0.5") - (-0.5)) < EPSILON);

    // 2. Scientific Notation (E-notation) - Core soft-float stress test
    print_test_status("Atof Scientific Positive (1e3)", 
        (atof("1e3") - 1000.0) < EPSILON);
    print_test_status("Atof Scientific Negative (1.5e-2)", 
        (atof("1.5e-2") - 0.015) < EPSILON); 
    
    // 3. Long decimal (stressing precision of soft-float division)
    print_test_status("Atof Long Decimal Precision", 
        (atof("3.14159265") - 3.14159265) < EPSILON);
}


/*
 * qsort Test Cases (Quicksort Algorithm)
 * Tests O(log N) stack space optimization via worst-case array.
 */
void test_qsort() {
    printf("\n--- Testing qsort (Robustness and LLM Data) ---\n");
    int success = 1;
    uint n;

    // 1. Integer Array (Unsorted)
    int arr_int[] = {50, 10, 30, 20, 40};
    int expected_int[] = {10, 20, 30, 40, 50};
    n = sizeof(arr_int) / sizeof(arr_int[0]);
    qsort(arr_int, n, sizeof(int), compare_ints);
    for (uint i = 0; i < n; i++) {
        if (arr_int[i] != expected_int[i]) { success = 0; break; }
    }
    print_test_status("Qsort 1: Integer Array (Unsorted)", success);
    
    // 2. Double Array (LLM Weights)
    double arr_double[] = {3.14, 1.05, 5.0, 0.01, 2.71};
    double expected_double[] = {0.01, 1.05, 2.71, 3.14, 5.0};
    n = sizeof(arr_double) / sizeof(arr_double[0]);
    qsort(arr_double, n, sizeof(double), compare_doubles);
    success = 1;
    for (uint i = 0; i < n; i++) {
        // Use double comparator for checking result
        if (compare_doubles(&arr_double[i], &expected_double[i]) != 0) { success = 0; break; }
    }
    print_test_status("Qsort 2: Double Array (LLM Weights)", success);

    // 3. Worst-Case Reverse Sorted (Tests O(log N) stack depth safety)
    int arr_rev[] = {10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
    int expected_rev[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    qsort(arr_rev, 10, sizeof(int), compare_ints);
    success = 1;
    for (int i = 0; i < 10; i++) {
        if (arr_rev[i] != expected_rev[i]) { success = 0; break; }
    }
    print_test_status("Qsort 3: Worst-Case (Reverse Array)", success);
}


/*
 * bsearch Test Cases (Binary Search)
 */
void test_bsearch() {
    printf("\n--- Testing bsearch ---\n");
    int arr[] = {10, 20, 30, 40, 50}; // Must be sorted!
    uint n = sizeof(arr) / sizeof(arr[0]);
    int *result;

    // 1. Found: Element in the middle (30)
    int key1 = 30;
    result = (int*)bsearch(&key1, arr, n, sizeof(int), compare_ints);
    print_test_status("Bsearch Found Middle (30)", (result != 0 && *result == 30));

    // 2. Found: Element at the boundary (10)
    int key2 = 10;
    result = (int*)bsearch(&key2, arr, n, sizeof(int), compare_ints);
    print_test_status("Bsearch Found Boundary (10)", (result != 0 && *result == 10));

    // 3. Not Found: Element between existing (25)
    int key3 = 25;
    result = (int*)bsearch(&key3, arr, n, sizeof(int), compare_ints);
    print_test_status("Bsearch Not Found Between (25)", result == 0);
}


// --- Main Entry Point ---

void main() {
    printf("============================================\n");
    printf(" XV6 Standard Library Milestone 2 Test Suite\n");
    printf("============================================\n");
    
    test_calloc();
    test_atoi();
    test_atof();
    test_qsort();
    test_bsearch();
    
    printf("\n--- Finished All Stdlib Tests ---\n");
    
    // Use exit() instead of return for user programs in xv6
    exit(0);
}