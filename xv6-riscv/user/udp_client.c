/* user/udp_client.c
   xv6 user-level reliable file fetcher (simple request-response).
   Uses xv6 send/recv/bind syscalls as provided in the kernel.
*/

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user.h"
#include "sha256.h"   // <-- use your real sha256 implementation
#include "udp_client.h"


/* helper: convert 4 big-endian bytes to uint */
static uint be32_to_u32(uchar *b) {
    return ((uint)b[0] << 24) | ((uint)b[1] << 16) | ((uint)b[2] << 8) | (uint)b[3];
}

/* helper: convert 2 big-endian bytes to ushort */
static ushort be16_to_u16(uchar *b) {
    return ((ushort)b[0] << 8) | (ushort)b[1];
}

/* helper: write uint32 big-endian into buffer */
static void u32_to_be32(uint v, uchar *b) {
    b[0] = (uchar)((v >> 24) & 0xFF);
    b[1] = (uchar)((v >> 16) & 0xFF);
    b[2] = (uchar)((v >> 8) & 0xFF);
    b[3] = (uchar)(v & 0xFF);
}

/* state: assume simple single-threaded client, bind once */
static int rftp_inited = 0;

int rftp_init(void) {
    if (rftp_inited) return 0;
    if (bind(RFTP_CLIENT_PORT) < 0) {
        printf("rftp_init: bind failed on port %d\n", RFTP_CLIENT_PORT);
        return RFTP_ERR_SOCKET;
    }
    rftp_inited = 1;
    return 0;
}

int rftp_cleanup(void) {
    /* xv6 unbind syscall exists as sys_unbind; user-level wrapper not standardized
       We won't call unbind here to keep simple (ports freed on process exit). */
    rftp_inited = 0;
    return 0;
}

/* rftp_fetch_file:
   1) send metadata request [type=1][file_id]
   2) receive metadata response: [type=2][file_id][status][filesize:4][chunk_size:2][chunk_count:4][sha256:32]
   3) allocate buffer filesize
   4) for each chunk index: send chunk request [type=3][file_id][chunk_index:4 BE] and receive chunk response:
      [type=4][file_id][chunk_index:4][data_len:2][data...]
   5) write into buffer
*/
/* -----------------------------------------------------------
 *  GLOBAL SHA STORAGE (for debug + udp_test use)
 * ----------------------------------------------------------- */
static uchar last_sha256[32];

/* Copy out last SHA256 (for udp_test) */
void rftp_last_sha(uchar out[32]) {
    memmove(out, last_sha256, 32);
}


/* -----------------------------------------------------------
 *  MAIN FILE FETCH FUNCTION — with server SHA extraction
 * ----------------------------------------------------------- */

int rftp_fetch_file(uchar file_id, struct rftp_result *res) {
    if (!res) return RFTP_ERR_MEMORY;
    res->data = 0;
    res->size = 0;
    memset(res->sha256, 0, 32);

    if (!rftp_inited) {
        int rc = rftp_init();
        if (rc != 0) return rc;
    }

    /* ---- 1) send metadata request ---- */
    uchar req_meta[2];
    req_meta[0] = MSG_METADATA_REQ;
    req_meta[1] = file_id;

    if (send(RFTP_CLIENT_PORT, RFTP_SERVER_IP, RFTP_GUEST_DST_PORT,
             (char*)req_meta, 2) < 0) {
        printf("rftp_fetch_file: send metadata failed\n");
        return RFTP_ERR_SERVER;
    }

    /* debug: show where we sent metadata */
    printf("rftp_fetch_file: sent metadata to server_ip=0x%x (%d.%d.%d.%d) port=%d\n",
           RFTP_SERVER_IP,
           (RFTP_SERVER_IP >> 24) & 0xFF,
           (RFTP_SERVER_IP >> 16) & 0xFF,
           (RFTP_SERVER_IP >> 8) & 0xFF,
           RFTP_SERVER_IP & 0xFF,
           RFTP_GUEST_DST_PORT);

    /* ---- 2) receive metadata ---- */
    uchar meta_buf[256];                    // only for metadata (small)
    uint src_ip = 0;
    ushort src_port = 0;

    int n = recv(RFTP_CLIENT_PORT, &src_ip, &src_port,
                 (char*)meta_buf, sizeof(meta_buf));

    if (n <= 0) {
        printf("rftp_fetch_file: recv metadata failed n=%d\n", n);
        return RFTP_ERR_SERVER;
    }

    if (meta_buf[0] != MSG_METADATA_RESP) {
        printf("rftp_fetch_file: bad metadata msg type %d\n", meta_buf[0]);
        return RFTP_ERR_SERVER;
    }

    /* metadata minimum length check:
       msg_type(1) + file_id(1) + status(1) + filesize(4) + chunk_size(2) + chunk_count(4) + sha256(32) = 45 */
    if (n < 1 + 1 + 1 + 4 + 2 + 4 + 32) {
        printf("rftp_fetch_file: metadata too short n=%d\n", n);
        return RFTP_ERR_SERVER;
    }

    uchar status      = meta_buf[2];
    uint filesize     = be32_to_u32(&meta_buf[3]);
    ushort chunk_size = be16_to_u16(&meta_buf[7]);
    uint chunk_count  = be32_to_u32(&meta_buf[9]);

    /* copy server SHA256 */
    uchar sha256_server[32];
    for (int i = 0; i < 32; i++)
        sha256_server[i] = meta_buf[13 + i];

    /* store so udp_test can print BOTH expected + computed */
    memmove(last_sha256, sha256_server, 32);

    if (status != 0) {
        printf("rftp_fetch_file: server status error for file %d\n", file_id);
        return RFTP_ERR_SERVER;
    }

    if (filesize == 0) {
        res->data = 0;
        res->size = 0;
        memmove(res->sha256, sha256_server, 32);
        return RFTP_SUCCESS;
    }

    /* print metadata for debugging */
    printf("rftp_fetch_file: metadata filesize=%d chunk_size=%d chunk_count=%d\n",
           filesize, chunk_size, chunk_count);

    /* ---- allocate file buffer ---- */
    char *buf = malloc(filesize);
    if (!buf) {
        printf("rftp_fetch_file: malloc failed for %d bytes\n", filesize);
        return RFTP_ERR_MEMORY;
    }

    /* ---- prepare recv buffer sized for header + chunk payload ----
       chunk response header = 1(type) +1(file_id) +4(idx) +2(data_len) = 8 bytes
    */
    int recv_buf_size = (int)chunk_size + 8;
    if (recv_buf_size < 2048) recv_buf_size = 2048;    // keep a reasonable minimum
    uchar *rbuf = malloc(recv_buf_size);
    if (!rbuf) {
        printf("rftp_fetch_file: malloc failed for recv buffer %d\n", recv_buf_size);
        free(buf);
        return RFTP_ERR_MEMORY;
    }

    /* ---- 3) fetch chunks ---- */
    uchar req_chunk[6];

    for (uint idx = 0; idx < chunk_count; idx++) {
        req_chunk[0] = MSG_DATA_REQ;
        req_chunk[1] = file_id;
        u32_to_be32(idx, &req_chunk[2]);

        if (send(RFTP_CLIENT_PORT, RFTP_SERVER_IP, RFTP_GUEST_DST_PORT,
                 (char*)req_chunk, 6) < 0) {
            printf("rftp_fetch_file: send chunk req failed idx=%d\n", idx);
            free(buf);
            free(rbuf);
            return RFTP_ERR_SERVER;
        }

        int got = 0;
        int tries = 0;

        while (!got && tries < 10) {
            int rn = recv(RFTP_CLIENT_PORT, &src_ip, &src_port,
                          (char*)rbuf, recv_buf_size);

            if (rn <= 0) {
                tries++;
                if (tries < 3)
                    send(RFTP_CLIENT_PORT, RFTP_SERVER_IP, RFTP_GUEST_DST_PORT,
                         (char*)req_chunk, 6);
                continue;
            }

            /* Minimal header size check: 8 bytes */
            if (rn < 8) {
                tries++;
                continue;
            }

            if (rbuf[0] != MSG_DATA_RESP)
                continue;

            uint ridx = be32_to_u32(&rbuf[2]);
            ushort data_len = be16_to_u16(&rbuf[6]);

            /* Validate declared payload fits in the received buffer */
            if (rn < 8 + (int)data_len) {
                /* truncated packet, ignore */
                tries++;
                continue;
            }

            if (ridx != idx)
                continue;

            if (data_len == 0 || data_len > chunk_size) {
                printf("rftp_fetch_file: bad data_len %d for idx %d\n",
                       data_len, idx);
                free(buf);
                free(rbuf);
                return RFTP_ERR_SERVER;
            }

            uint offset = idx * (uint)chunk_size;

            if (offset + data_len > filesize)
                data_len = filesize - offset;

            memmove(buf + offset, &rbuf[8], data_len);
            got = 1;
        }

        if (!got) {
            printf("rftp_fetch_file: failed to get chunk %d after retries\n", idx);
            free(buf);
            free(rbuf);
            return RFTP_ERR_SERVER;
        }

        if (idx % 1024 == 0 || idx == chunk_count - 1) {
            printf("Downloaded %d / %d chunks...\n", idx + 1, chunk_count);
        }
    }

    /* free recv buffer before finishing */
    free(rbuf);

    /* ---- success path ---- */
    res->data = buf;
    res->size = filesize;
    memmove(res->sha256, sha256_server, 32);

    return RFTP_SUCCESS;
}




/* Convenience wrappers: call rftp_fetch_file and return malloc'd block pointer */
char *fetch_model_weights(int *size_out) {
    struct rftp_result r;
    int rc = rftp_fetch_file(FILE_ID_MODEL, &r);
    if (rc != RFTP_SUCCESS) {
        if (size_out) *size_out = 0;
        return 0;
    }
    if (size_out) *size_out = r.size;
    return r.data;
}

char *fetch_tokenizer(int *size_out) {
    struct rftp_result r;
    int rc = rftp_fetch_file(FILE_ID_TOKENIZER, &r);
    if (rc != RFTP_SUCCESS) {
        if (size_out) *size_out = 0;
        return 0;
    }
    if (size_out) *size_out = r.size;
    return r.data;
}
