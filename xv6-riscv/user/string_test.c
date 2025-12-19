/*
 * Run as a userspace test. Prints expected vs actual and returns 0 for all pass, 1 otherwise.
 * Each test prints PASS/FAIL.
 */

#include "user.h"   // for printf()
#include "kernel/types.h"
#include <stdarg.h>
#include <stddef.h>

/* Forward declarations for functions moved to ulib.c (not present in string.h) */
/* e.g. memcpy/memset/strcmp/strlen/strcpy/vsnprintf/sscanf are implemented separately */

static int tests_failed = 0;

#define ASSERT_STR_EQ(name, expected, actual) do { \
    if (strcmp((expected),(actual))==0) { \
        printf("[PASS] %s: expected \"%s\", got \"%s\"\n", (name),(expected),(actual)); \
    } else { \
        printf("[FAIL] %s: expected \"%s\", got \"%s\"\n", (name),(expected),(actual)); tests_failed++; \
    } \
} while(0)

#define ASSERT_INT_EQ(name, expected, actual) do { \
    if ((expected)==(actual)) { \
        printf("[PASS] %s: expected %d, got %d\n", (name),(expected),(actual)); \
    } else { \
        printf("[FAIL] %s: expected %d, got %d\n", (name),(expected),(actual)); tests_failed++; \
    } \
} while(0)

/* Safe helper to call vsnprintf without fabricating a va_list.
   Use this to exercise vsnprintf with a fixed format string. */
static void call_vsnprintf(char *buf, size_t bufsz, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, bufsz, fmt, ap);
    va_end(ap);
}

/* ---------- Tests ---------- */

void test_strcmp_strlen_strcpy() {
    
    ASSERT_INT_EQ("strcmp equal", 0, strcmp("abc","abc"));
    /* strcmp sign check */
    int r = strcmp("abc", "abd");
    if (r < 0) printf("[PASS] strcmp less\n"); else { printf("[FAIL] strcmp less: %d\n", r); tests_failed++; }
    r = strcmp("abe", "abd");
    if (r > 0) printf("[PASS] strcmp greater\n"); else { printf("[FAIL] strcmp greater: %d\n", r); tests_failed++; }
    ASSERT_INT_EQ("strlen normal", 5, (int)strlen("Hello"));
    ASSERT_INT_EQ("strlen empty", 0, (int)strlen(""));
    char buf[16];
    strcpy(buf, "copyme");

    ASSERT_STR_EQ("strcpy", "copyme", buf);
}

void test_isprint_isspace() {
    if (isprint('A')) printf("[PASS] isprint A\n"); else { printf("[FAIL] isprint A\n"); tests_failed++; }
    if (!isprint('\n')) printf("[PASS] isprint newline\n"); else { printf("[FAIL] isprint newline\n"); tests_failed++; }

    if (isspace(' ')) printf("[PASS] isspace space\n"); else { printf("[FAIL] isspace space\n"); tests_failed++; }
    if (isspace('\t')) printf("[PASS] isspace tab\n"); else { printf("[FAIL] isspace tab\n"); tests_failed++; }
}

void test_memset() {
    char b[8];
    memset(b, 'A', sizeof(b)-1);
    b[7] = '\0';
    ASSERT_STR_EQ("memset normal", "AAAAAAA", b);

    memset(b, 0, sizeof(b));
    /* Note: printing strings containing embedded NULs will look empty; still the comparison checks the first NUL */
    ASSERT_STR_EQ("memset zero", "\0\0\0\0\0\0\0\0", b);

    char c[4];
    memset(c, 'z', 3);
    c[3] = '\0';
    ASSERT_STR_EQ("memset small", "zzz", c);
}

void test_sprintf() {
    char buf[128];

    /* 1. Integer formatting */
    printf("about to sprintf int\n");
    sprintf(buf, "Number: %d", 42);
    printf("done sprintf int\n");

    ASSERT_STR_EQ("sprintf int", "Number: 42", buf);

    /* 2. String formatting */
    sprintf(buf, "Hello %s", "world");
    ASSERT_STR_EQ("sprintf str", "Hello world", buf);

    /* 3. Float formatting precision */
    printf("about to sprintf float\n");
    sprintf(buf, "Pi: %.4f", 3.14159);
    printf("done sprintf float\n");
    ASSERT_STR_EQ("sprintf float", "Pi: 3.1416", buf); /* note rounding */

    /* 4. Mixed formatting */
    sprintf(buf, "%s scored %f (%.2f%%)", "Alice", 95.0, 95.5);
    ASSERT_STR_EQ("sprintf mixed", "Alice scored 95.000000 (95.50%)", buf);

    /* 5. Edge cases */
    sprintf(buf, "percent %% done");
    ASSERT_STR_EQ("sprintf percent", "percent % done", buf);

    /* buffer boundary test (very small) -- call vsnprintf through wrapper (do NOT fabricate a va_list) */
    char small[8];
    call_vsnprintf(small, sizeof(small), "ABCDEFGH12345");
    printf("[INFO] small buffer content: \"%s\"\n", small);
}

void test_sscanf() {
    int i; double d; char s[32]; char c;
    int n;
    n = sscanf("123 abc 3.14 z", "%d %s %f %c", &i, s, &d, &c);
    ASSERT_INT_EQ("sscanf conversions", 4, n);
    ASSERT_INT_EQ("sscanf int value", 123, i);
    ASSERT_STR_EQ("sscanf string value", "abc", s);

    /* check approx double */
    if (d > 3.13 && d < 3.15) printf("[PASS] sscanf double approx\n"); else { printf("[FAIL] sscanf double approx: %f\n", d); tests_failed++; }

    /* build a NUL-terminated 1-char string for comparison */
    char cs[2] = { c, '\0' };
    ASSERT_STR_EQ("sscanf char", "z", cs);
}

int main(void) {

    test_strcmp_strlen_strcpy();

    test_sprintf();
    test_isprint_isspace();

    test_sscanf();
    test_memset();

    if (tests_failed == 0) {
        printf("ALL TESTS PASSED\n");
        return 0;
    } else {
        printf("%d test(s) failed\n", tests_failed);
        return 1;
    }
}
