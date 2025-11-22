// user/sha256.c
// Self-contained SHA-256 user-level implementation for xv6 with tests.
// Use only xv6 user APIs (printf, exit).

#include "kernel/types.h"
#include "sha256.h"
#include "user.h"    // xv6 user-level functions like printf, exit, strlen



/* Right rotate macro */
#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

/* SHA-256 constants */
static const uint32 K[64] = {
  0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
  0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
  0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
  0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
  0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
  0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
  0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
  0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u
};

/* Initial hash values (first 32 bits of the fractional parts of the square roots of the first 8 primes) */
static const uint32 H0_init[8] = {
  0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u
};

/* Process one 512-bit block. 'block' points to 64 bytes. 'H' is the current state (8 words). */
static void sha256_process_block(const uchar block[64], uint32 H[8]) {
  uint32 W[64];
  int t;

  /* Prepare message schedule W in big-endian */
  for (t = 0; t < 16; ++t) {
    int i = t * 4;
    W[t] = ((uint32)block[i] << 24) |
           ((uint32)block[i+1] << 16) |
           ((uint32)block[i+2] << 8) |
           ((uint32)block[i+3]);
  }
  for (t = 16; t < 64; ++t) {
    uint32 s0 = ROR32(W[t-15], 7) ^ ROR32(W[t-15], 18) ^ (W[t-15] >> 3);
    uint32 s1 = ROR32(W[t-2], 17) ^ ROR32(W[t-2], 19) ^ (W[t-2] >> 10);
    W[t] = W[t-16] + s0 + W[t-7] + s1;
  }

  uint32 a = H[0], b = H[1], c = H[2], d = H[3];
  uint32 e = H[4], f = H[5], g = H[6], h = H[7];

  for (t = 0; t < 64; ++t) {
    uint32 S1 = ROR32(e, 6) ^ ROR32(e, 11) ^ ROR32(e, 25);
    uint32 ch = (e & f) ^ ((~e) & g);
    uint32 temp1 = h + S1 + ch + K[t] + W[t];
    uint32 S0 = ROR32(a, 2) ^ ROR32(a, 13) ^ ROR32(a, 22);
    uint32 maj = (a & b) ^ (a & c) ^ (b & c);
    uint32 temp2 = S0 + maj;

    h = g;
    g = f;
    f = e;
    e = d + temp1;
    d = c;
    c = b;
    b = a;
    a = temp1 + temp2;
  }

  H[0] += a; H[1] += b; H[2] += c; H[3] += d;
  H[4] += e; H[5] += f; H[6] += g; H[7] += h;
}

/* Public API: compute SHA-256 hash */
void sha256_hash(const uchar *data, unsigned int len, uchar hash[32]) {
  uint32 H[8];
  int i;
  for (i = 0; i < 8; ++i) H[i] = H0_init[i];

  unsigned int rem = len;
  const uchar *p = data;
  while (rem >= 64) {
    sha256_process_block(p, H);
    p += 64; rem -= 64;
  }

  uchar block[64];
  for (i = 0; i < (int)rem; ++i) block[i] = p[i];
  block[rem] = 0x80u;

  if (rem >= 56) {
    for (i = rem + 1; i < 64; ++i) block[i] = 0;
    sha256_process_block(block, H);
    for (i = 0; i < 56; ++i) block[i] = 0;
  } else {
    for (i = rem + 1; i < 56; ++i) block[i] = 0;
  }

  uint64 bitlen = (uint64)len * 8ULL;
  block[56] = (uchar)((bitlen >> 56) & 0xff);
  block[57] = (uchar)((bitlen >> 48) & 0xff);
  block[58] = (uchar)((bitlen >> 40) & 0xff);
  block[59] = (uchar)((bitlen >> 32) & 0xff);
  block[60] = (uchar)((bitlen >> 24) & 0xff);
  block[61] = (uchar)((bitlen >> 16) & 0xff);
  block[62] = (uchar)((bitlen >> 8) & 0xff);
  block[63] = (uchar)(bitlen & 0xff);

  sha256_process_block(block, H);

  for (i = 0; i < 8; ++i) {
    hash[i*4 + 0] = (uchar)((H[i] >> 24) & 0xff);
    hash[i*4 + 1] = (uchar)((H[i] >> 16) & 0xff);
    hash[i*4 + 2] = (uchar)((H[i] >> 8) & 0xff);
    hash[i*4 + 3] = (uchar)(H[i] & 0xff);
  }
}

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

/* ---------- Test vectors in main() ---------- */
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
