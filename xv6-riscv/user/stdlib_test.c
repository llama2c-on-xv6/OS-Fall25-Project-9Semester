// user/stdlib_test.c
#include "kernel/types.h"
#include "user/user.h"
#include "user/stdlib.h"

#define EPS 1e-5

static int tests_failed = 0;
static int tests_run = 0;

static void ok(int cond, const char *name) {
    tests_run++;
    if (cond) {
        printf("[PASS] %s\n", name);
    } else {
        printf("[FAIL] %s\n", name);
        tests_failed++;
    }
}

/* Helpers for qsort/bsearch comparators */
int cmp_int(const void *a, const void *b) {
    int va = *(const int*)a;
    int vb = *(const int*)b;
    return (va < vb) ? -1 : (va > vb) ? 1 : 0;
}
int cmp_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int main(void) {
    // ---------- calloc tests ----------
    int *arr = (int*)calloc(5, sizeof(int));
    ok(arr != 0, "calloc non-null allocation");
    if (arr) {
        int all_zero = 1;
        for (int i = 0; i < 5; i++) if (arr[i] != 0) { all_zero = 0; break; }
        ok(all_zero, "calloc array zeroed");
        free(arr);
    }

    void *p0 = calloc(0, 0);
    ok(p0 != 0, "calloc size 0 returns non-null (implementation-defined accepted)");
    if (p0) free(p0);

    // ---------- qsort tests ----------
    int a[] = {5, 3, 7, 1, 4};
    qsort(a, 5, sizeof(int), cmp_int);
    int sorted_ok = 1;
    for (int i = 0; i < 4; i++) if (a[i] > a[i+1]) { sorted_ok = 0; break; }
    ok(sorted_ok, "qsort sorts integers");

    double b[] = {3.14, -1.0, 0.001, 42.0, 3.14};
    qsort(b, 5, sizeof(double), cmp_double);
    sorted_ok = 1;
    for (int i = 0; i < 4; i++) if (b[i] > b[i+1] + EPS) { sorted_ok = 0; break; }
    ok(sorted_ok, "qsort sorts doubles");

    int single = 7;
    qsort(&single, 1, sizeof(int), cmp_int);
    ok(single == 7, "qsort single element unchanged");

    // ---------- bsearch tests ----------
    int s1[] = {1, 3, 5, 7, 9};
    int key = 5;
    int *res = bsearch(&key, s1, 5, sizeof(int), cmp_int);
    ok(res != 0 && *res == 5, "bsearch finds existing element");

    int key2 = 2;
    res = bsearch(&key2, s1, 5, sizeof(int), cmp_int);
    ok(res == 0, "bsearch returns NULL for missing element");

    int s_single[] = {42};
    int k42 = 42;
    res = bsearch(&k42, s_single, 1, sizeof(int), cmp_int);
    ok(res != 0 && *res == 42, "bsearch finds in single-element array");

    res = bsearch(&k42, s_single, 0, sizeof(int), cmp_int);
    ok(res == 0, "bsearch returns NULL for empty array");

    // ---------- atoi tests ----------
    ok(atoi("123") == 123, "atoi positive number");
    ok(atoi("-45") == -45, "atoi negative number");
    ok(atoi("   78") == 78, "atoi leading whitespace");
    ok(atoi("abc") == 0, "atoi invalid input returns 0");

    // ---------- atof tests ----------
    double d1 = atof("42");
    ok( (d1 >= 42.0 - EPS && d1 <= 42.0 + EPS), "atof integer as float");

    double d2 = atof(" -12.345 ");
    ok( (d2 >= -12.345 - 1e-4 && d2 <= -12.345 + 1e-4), "atof fractional number");

    double d3 = atof("1.5e-3");
    ok( (d3 >= 0.0015 - 1e-8 && d3 <= 0.0015 + 1e-8), "atof scientific notation 1.5e-3");

    double d4 = atof("-2.0E2");
    ok( (d4 >= -200.0 - 1e-6 && d4 <= -200.0 + 1e-6), "atof negative with exponent");

    double d5 = atof("   3.14");
    ok( (d5 >= 3.14 - 1e-6 && d5 <= 3.14 + 1e-6), "atof leading whitespace");

    // summary
    printf("---- summary: %d tests run, %d failed ----\n", tests_run, tests_failed);
    return (tests_failed == 0) ? 0 : 1;
}
