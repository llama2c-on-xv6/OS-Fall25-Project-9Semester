/*
 * RFTP Client Library Header
 * Place in: xv6-riscv/user/udp_client.h
 */

#ifndef UDP_CLIENT_H
#define UDP_CLIENT_H

#include "kernel/types.h"

/* Protocol version and constants */
#define RFTP_VERSION 1
#define RFTP_PORT 25999
#define RFTP_SERVER_IP 0x0A000202  /* 10.0.2.2 */
#define RFTP_MAX_PAYLOAD 512
#define RFTP_BATCH_SIZE 16
#define RFTP_MAX_RETRIES 10
#define RFTP_TIMEOUT_ITERS 1000000

/* Message types */
#define MSG_METADATA_REQ 0x01
#define MSG_METADATA_RESP 0x02
#define MSG_DATA_REQ 0x03
#define MSG_DATA_RESP 0x04
#define MSG_BATCH_REQ 0x05
#define MSG_ERROR 0xFF

/* File IDs */
#define FILE_ID_MODEL 0x01
#define FILE_ID_TOKENIZER 0x02

/* Error codes */
#define ERR_NONE 0x00
#define ERR_INVALID_FILE 0x01
#define ERR_INVALID_PACKET 0x02
#define ERR_SERVER_BUSY 0x03

/* Return codes */
#define RFTP_SUCCESS 0
#define RFTP_ERR_SOCKET -1
#define RFTP_ERR_TIMEOUT -2
#define RFTP_ERR_SERVER -3
#define RFTP_ERR_MEMORY -4
#define RFTP_ERR_INTEGRITY -5

/* ===== Protocol Message Structures ===== */

/* Generic header (4 bytes) */
struct rftp_header {
    uchar version;
    uchar msg_type;
    uchar seq_hi;
    uchar seq_lo;
};

/* METADATA_REQUEST: 5 bytes */
struct metadata_request {
    struct rftp_header header;
    uchar file_id;
};

/* METADATA_RESPONSE: 46 bytes */
struct metadata_response {
    struct rftp_header header;
    uchar file_id;
    uchar error_code;
    uchar size_b3, size_b2, size_b1, size_b0;      /* 4 bytes: file size */
    uchar pktcnt_b3, pktcnt_b2, pktcnt_b1, pktcnt_b0;  /* 4 bytes: packet count */
    uchar sha256_hash[32];                          /* 32 bytes: SHA-256 hash */
};

/* DATA_REQUEST: 9 bytes */
struct data_request {
    struct rftp_header header;
    uchar file_id;
    uchar idx_b3, idx_b2, idx_b1, idx_b0;
};

/* DATA_RESPONSE header: 14 bytes + payload */
struct data_response_hdr {
    struct rftp_header header;
    uchar file_id;
    uchar error_code;
    uchar idx_b3, idx_b2, idx_b1, idx_b0;          /* packet index */
    uchar len_hi, len_lo;                          /* payload length */
    uchar csum_hi, csum_lo;                        /* XOR checksum */
};

/* BATCH_REQUEST: 6 + (4 * count) bytes */
struct batch_request {
    struct rftp_header header;
    uchar file_id;
    uchar count;
    uchar indices[64];  /* Up to 16 * 4-byte indices */
};

/* Result structure returned by fetch functions */
struct rftp_result {
    char *data;
    uint size;
    uchar sha256[32];
    uint packets_received;
    uint retries;
};

/* ===== Public API ===== */

/* Initialize RFTP client (binds to local port) */
int rftp_init(void);

/* Cleanup RFTP client */
void rftp_cleanup(void);

/* Fetch a file by ID, returns 0 on error */
int rftp_fetch_file(uchar file_id, struct rftp_result *result);

/* Convenience wrappers: fetch model and tokenizer */
char* fetch_model_weights(int *size_out);
char* fetch_tokenizer(int *size_out);

#endif /* UDP_CLIENT_H */