/* user/udp_test.c
   Test program for UDP file fetcher + parsing tokenizer format used by the Python Tokenizer.export()
*/

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user.h"
#include "sha256.h"
#include "udp_client.h"

/* print hex bytes WITHOUT using %02x (xv6 printf doesn't support width/zero-pad) */
static void hex_print(const uchar *d, int n) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < n; i++) {
        uchar hi = (d[i] >> 4) & 0xF;
        uchar lo = d[i] & 0xF;
        printf("%c", hex[hi]);
        printf("%c", hex[lo]);
    }
    printf("\n");
}

/* read 32-bit little-endian */
static uint le32(const uchar *p) {
    return ((uint)p[0]) | ((uint)p[1] << 8) | ((uint)p[2] << 16) | ((uint)p[3] << 24);
}

/* float little-endian: reinterpret 4 little-endian bytes as IEEE-754 float */
static float lefloat(const uchar *p) {
    uint v = le32(p);
    float f;
    memmove(&f, &v, sizeof(f));
    return f;
}

int main(int argc, char *argv[]) {
    printf("udp_test: starting\n");

    if (rftp_init() != 0) {
        printf("udp_test: rftp_init failed\n");
        exit(1);
    }

    /* ============================
       FETCH MODEL
    ============================ */
    int model_size = 0;
    char *model = fetch_model_weights(&model_size);
    if (!model) {
        printf("udp_test: fetch_model_weights failed\n");
        rftp_cleanup();
        exit(1);
    }
    printf("udp_test: fetched model size=%d bytes\n", model_size);

    /* compute SHA-256 */
    uchar digest[32];
    sha256((uchar*)model, (uint)model_size, digest);

    /* print computed SHA */
    printf("udp_test: computed model sha256: ");
    hex_print(digest, 32);

    /* PRINT SERVER SHA FROM INTERNAL STATE (udp_client.c writes this) */
    uchar server_sha[32];
    rftp_last_sha(server_sha);
    printf("udp_test: metadata sha256 from server: ");
    hex_print(server_sha, 32);

    /* compare */
    int ok = 1;
    for (int i = 0; i < 32; i++) {
        if (digest[i] != server_sha[i]) { ok = 0; break; }
    }

    if (ok) {
        printf("udp_test: SHA256 MATCH\n");
    } else {
        printf("udp_test: SHA256 MISMATCH\n");
    }

    /* ============================
       FETCH TOKENIZER
    ============================ */
    int tok_size = 0;
    char *tok = fetch_tokenizer(&tok_size);
    if (!tok) {
        printf("udp_test: fetch_tokenizer failed\n");
        free(model);
        rftp_cleanup();
        exit(1);
    }
    printf("udp_test: fetched tokenizer size=%d bytes\n", tok_size);

    uchar *p = (uchar*)tok;
    uchar *end = p + tok_size;

    if (p + 4 > end) {
        printf("udp_test: tokenizer too small\n");
        free(model); free(tok); rftp_cleanup(); exit(1);
    }

    uint max_token_len = le32(p);
    p += 4;
    printf("udp_test: tokenizer max_token_length=%d\n", max_token_len);

    int token_count = 0;
    while (p + 8 <= end) {
        float score = lefloat(p); p += 4;
        uint len_bytes = le32(p); p += 4;

        if (p + len_bytes > end) {
            printf("udp_test: token parse overflow\n");
            break;
        }

        if (token_count < 10) {
            /* convert to signed fixed-point thousandths without using %f */
            int s1000 = (int)(score * 1000.0f);
            int sign = 1;
            if (s1000 < 0) { sign = -1; s1000 = -s1000; }
            int whole = s1000 / 1000;
            int frac = s1000 % 1000; /* 0..999 */

            /* print header */
            printf("token[%d]: len=%d score=", token_count, len_bytes);

            if (sign < 0) printf("-");

            /* print whole part */
            printf("%d.", whole);

            /* print exactly three digits for frac (manual, no %03d) */
            char d1 = '0' + (frac / 100) % 10;
            char d2 = '0' + (frac / 10) % 10;
            char d3 = '0' + (frac % 10);
            printf("%c", d1); printf("%c", d2); printf("%c", d3);


            /* print token text safely, showing non-printable as \xNN */
            printf(" text=\"");
            for (uint j = 0; j < len_bytes; j++) {
                uchar ch = p[j];
                if (ch >= 32 && ch < 127) {
                    printf("%c", ch);        // printable ASCII
                } else {
                    printf("\\x");           // non-printable: show hex
                    static const char hex[] = "0123456789abcdef";
                    printf("%c%c", hex[(ch >> 4) & 0xF], hex[ch & 0xF]);
                }
            }
            printf("\"\n");
        }

        p += len_bytes;
        token_count++;
    }

    printf("udp_test: parsed %d tokens\n", token_count);

    /* cleanup */
    free(model);
    free(tok);
    rftp_cleanup();

    printf("udp_test: done\n");
    exit(0);
}
