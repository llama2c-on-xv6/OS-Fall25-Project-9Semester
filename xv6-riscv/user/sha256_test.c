// user/sha256_test.c
// SHA-256 test program (standalone testing)
// Add to UPROGS if you want to test SHA-256 separately
// Optional: $ sha256_test

#include "kernel/types.h"
#include "sha256.h"
#include "user.h"

/* ---------- Helpers for test and printing ---------- */

static void to_hex(const uchar in[32], char out[65]) {
  static const char hex[] = "0123456789abcdef";
  int i;
  for (i = 0; i < 32; ++i) {
    out[i*2] = hex[(in[i] >> 4) & 0xf];
    out[i*2 + 1] = hex[in[i] & 0xf];
  }
  out[64] = 0;
}

static int hexstr_equal(const char *a, const char *b) {
  int i = 0;
  while (a[i] && b[i]) {
    if (a[i] != b[i]) return 0;
    ++i;
  }
  return a[i] == 0 && b[i] == 0;
}

/* ---------- Test vectors ---------- */
int main(void) {
  struct {
    const char *name;
    const uchar *data;
    unsigned int len;
    const char *expected_hex;
  } tests[] = {
    { "empty", (const uchar*)"", 0u,
      "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
    { "a", (const uchar*)"a", 1u,
      "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
    { "hello world", (const uchar*)"hello world", 11u,
      "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9" },
    { "block-boundary", (const uchar*)"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456", 59u,
      "60d0ba2d3510c243f1b619dac382d6a7dee50eb02f871e59c1066f728c7bd802" },
    { "multi-block", (const uchar*)"The quick brown fox jumps over the lazy dog. This is a longer test string that spans multiple blocks.", 
      101u,
      "65dab9c0a2772f0ea4654aabc5cb63c83a6ee018249ef5d104bed2ad7141a9e1" },
  };

  int ntests = sizeof(tests) / sizeof(tests[0]);
  int i;
  for (i = 0; i < ntests; ++i) {
    uchar out[32];
    char hexout[65];
    sha256_hash(tests[i].data, tests[i].len, out);
    to_hex(out, hexout);
    int pass = hexstr_equal(hexout, tests[i].expected_hex);
    printf("%s: expected %s\n", tests[i].name, tests[i].expected_hex);
    printf("%s: computed %s\n", tests[i].name, hexout);
    printf("%s: %s\n\n", tests[i].name, pass ? "PASS" : "FAIL");
    if (!pass) exit(1);
  }

  printf("All tests PASS\n");
  exit(0);
  return 0;
}