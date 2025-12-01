// user/stdlib.c
#include "kernel/types.h"
#include "user/user.h"
#include "user/stdlib.h"

/* swap_generic: swap two blocks of memory (size bytes) */
static void swap_generic(void *a, void *b, uint size) {
    unsigned char *p = (unsigned char*)a;
    unsigned char *q = (unsigned char*)b;
    for (uint i = 0; i < size; i++) {
        unsigned char tmp = p[i];
        p[i] = q[i];
        q[i] = tmp;
    }
}

/* calloc: allocate and zero memory. If n*size overflows, return 0.
   Behavior for n==0 or size==0: allocate 1 byte and return pointer (non-null).
*/
void* calloc(uint n, uint size) {
    if (n > 0 && size > 0) {
        uint64 total64 = (uint64)n * (uint64)size;
        if ((uint)total64 != total64) return 0; // overflow
        uint total = (uint)total64;
        void *p = malloc(total);
        if (!p) return 0;
        unsigned char *bp = (unsigned char*)p;
        for (uint i = 0; i < total; i++) bp[i] = 0;
        return p;
    } else {
        void *p = malloc(1);
        if (!p) return 0;
        ((unsigned char*)p)[0] = 0;
        return p;
    }
}

/* atoi: skip leading whitespace, handle optional +/- sign, parse digits */
int atoi(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;
    int sign = 1;
    if (*s == '+') s++;
    else if (*s == '-') { sign = -1; s++; }

    long long val = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10 + (*s - '0');
        s++;
    }
    return (int)(val * sign);
}

/* atof: parse float/double with optional exponent (returns double) */
double atof(const char *s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\f' || *s == '\v') s++;

    int sign = 1;
    if (*s == '+') { s++; }
    else if (*s == '-') { sign = -1; s++; }

    double val = 0.0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0 + (double)(*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        double place = 1.0;
        while (*s >= '0' && *s <= '9') {
            place *= 10.0;
            val = val * 10.0 + (double)(*s - '0');
            s++;
        }
        val = val / place;
    }

    if (*s == 'e' || *s == 'E') {
        s++;
        int exp_sign = 1;
        if (*s == '+') { s++; }
        else if (*s == '-') { exp_sign = -1; s++; }
        int exp_val = 0;
        while (*s >= '0' && *s <= '9') {
            exp_val = exp_val * 10 + (*s - '0');
            s++;
        }
        /* fast pow10 (integer exponent) */
        double pow10 = 1.0;
        double base10 = 10.0;
        int e = exp_val;
        while (e > 0) {
            if (e & 1) pow10 *= base10;
            base10 *= base10;
            e >>= 1;
        }
        if (exp_sign < 0) val /= pow10;
        else val *= pow10;
    }

    return sign * val;
}

/* ---------- qsort helper (file-scope recursive) ---------- */
/* qsort_rec sorts buffer `b` containing `n` elements of `size` bytes,
   using compare() as the comparator.
*/
static void qsort_rec(char *b, uint n, uint size, int (*compare)(const void *, const void *)) {
    if (n <= 1) return;

    uint low = 0, high = n - 1;
    uint mid = low + (high - low) / 2;
    char *ptr_low = b + low * size;
    char *ptr_mid = b + mid * size;
    char *ptr_high = b + high * size;

    if (compare(ptr_mid, ptr_low) < 0) swap_generic(ptr_mid, ptr_low, size);
    if (compare(ptr_high, ptr_low) < 0) swap_generic(ptr_high, ptr_low, size);
    if (compare(ptr_high, ptr_mid) < 0) swap_generic(ptr_high, ptr_mid, size);

    /* use pivot at ptr_high (median-of-three moved pivot to ptr_high) */
    uint i = 0;
    for (uint j = 0; j < high; j++) {
        char *pj = b + j * size;
        if (compare(pj, ptr_high) <= 0) {
            char *pi = b + i * size;
            swap_generic(pi, pj, size);
            i++;
        }
    }
    char *pivdest = b + i * size;
    swap_generic(pivdest, ptr_high, size);

    uint left_size = i;
    uint right_size = n - i - 1;

    if (left_size < right_size) {
        qsort_rec(b, left_size, size, compare);
        qsort_rec(b + (i + 1) * size, right_size, size, compare);
    } else {
        qsort_rec(b + (i + 1) * size, right_size, size, compare);
        qsort_rec(b, left_size, size, compare);
    }
}

/* qsort: entry point */
void qsort(void *base, uint n_elem, uint size, int (*compare)(const void *, const void *)) {
    if (n_elem <= 1) return;
    qsort_rec((char*)base, n_elem, size, compare);
}

/* bsearch: standard binary search on sorted array */
void* bsearch(const void *key, const void *base, uint n_elem, uint size, int (*compare)(const void *, const void *)) {
    int low = 0;
    int high = (int)n_elem - 1;
    while (low <= high) {
        int mid = low + (high - low) / 2;
        const void *mid_ptr = (const char*)base + (uint)mid * size;
        int cmp = compare(key, mid_ptr);
        if (cmp < 0) high = mid - 1;
        else if (cmp > 0) low = mid + 1;
        else return (void*)mid_ptr;
    }
    return 0;
}
