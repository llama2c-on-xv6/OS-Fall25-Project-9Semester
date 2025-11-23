/*
 * RFTP Client Library for xv6
 * Reliable File Transfer Protocol - Implementation
 * 
 * Place in: xv6-riscv/user/udp_client.c
 */

#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "udp_client.h"
#include "sha256.h"

/* Global state */
static int g_initialized = 0;
static uint16 g_local_port = 12345;
static uint16 g_seq_num = 0;

/* Helper: convert 4 bytes to uint (big-endian) */
static uint bytes_to_uint(uchar b3, uchar b2, uchar b1, uchar b0) {
    return ((uint)b3 << 24) | ((uint)b2 << 16) | 
           ((uint)b1 << 8) | (uint)b0;
}

/* Helper: convert 2 bytes to ushort (big-endian) */
static ushort bytes_to_ushort(uchar hi, uchar lo) {
    return ((ushort)hi << 8) | (ushort)lo;
}

/* Helper: write uint to buffer (big-endian) */
static void uint_to_bytes(uint val, uchar *buf) {
    buf[0] = (val >> 24) & 0xFF;
    buf[1] = (val >> 16) & 0xFF;
    buf[2] = (val >> 8) & 0xFF;
    buf[3] = val & 0xFF;
}

/* Helper: compute XOR checksum */
static ushort compute_checksum(uchar *data, int len) {
    ushort sum = 0;
    for (int i = 0; i < len; i += 2) {
        ushort word = data[i];
        if (i + 1 < len) {
            word |= ((ushort)data[i + 1] << 8);
        }
        sum ^= word;
    }
    return sum;
}

/* Helper: simple memset */
static void mymemset(void *dst, int val, int len) {
    uchar *p = (uchar *)dst;
    for (int i = 0; i < len; i++) p[i] = (uchar)val;
}

/* Helper: simple memcpy */
static void mymemcpy(void *dst, const void *src, int len) {
    uchar *d = (uchar *)dst;
    const uchar *s = (const uchar *)src;
    for (int i = 0; i < len; i++) d[i] = s[i];
}

/* Helper: simple memcmp */
static int mymemcmp(const void *a, const void *b, int len) {
    const uchar *pa = (const uchar *)a;
    const uchar *pb = (const uchar *)b;
    for (int i = 0; i < len; i++) {
        if (pa[i] != pb[i]) return pa[i] - pb[i];
    }
    return 0;
}

/* Helper: print hex bytes */
static void print_hex(const char *label, uchar *data, int len) {
    printf("%s: ", label);
    for (int i = 0; i < len; i++) {
        printf("%x%x", (data[i] >> 4) & 0xF, data[i] & 0xF);
    }
    printf("\n");
}

/* Initialize RFTP client */
int rftp_init(void) {
    if (g_initialized) return RFTP_SUCCESS;
    
    /* Bind to local port for receiving */
    if (bind(g_local_port) < 0) {
        printf("rftp: failed to bind port %d\n", g_local_port);
        return RFTP_ERR_SOCKET;
    }
    
    g_initialized = 1;
    return RFTP_SUCCESS;
}

/* Cleanup */
void rftp_cleanup(void) {
    /* unbind would go here if implemented */
    g_initialized = 0;
}

/* Send metadata request and get response */
static int request_metadata(uchar file_id, struct metadata_response *resp) {
    struct metadata_request req;
    uchar recv_buf[64];
    uint32 src_ip;
    uint16 src_port;
    int retries = 0;
    
    /* Build request */
    req.header.version = RFTP_VERSION;
    req.header.msg_type = MSG_METADATA_REQ;
    req.header.seq_hi = (g_seq_num >> 8) & 0xFF;
    req.header.seq_lo = g_seq_num & 0xFF;
    req.file_id = file_id;
    
    while (retries < RFTP_MAX_RETRIES) {
        /* Send request */
        if (send(g_local_port, RFTP_SERVER_IP, RFTP_PORT, 
                 (char*)&req, sizeof(req)) < 0) {
            printf("rftp: send failed\n");
            retries++;
            continue;
        }
        
        /* Wait for response with timeout */
        int got_response = 0;
        for (long i = 0; i < RFTP_TIMEOUT_ITERS && !got_response; i++) {
            int n = recv(g_local_port, &src_ip, &src_port, 
                        (char*)recv_buf, sizeof(recv_buf));
            if (n > 0) {
                got_response = 1;
                break;
            }
        }
        
        if (!got_response) {
            printf("rftp: metadata timeout (retry %d)\n", retries + 1);
            retries++;
            continue;
        }
        
        /* Check response */
        if (recv_buf[1] == MSG_METADATA_RESP) {
            mymemcpy(resp, recv_buf, sizeof(struct metadata_response));
            if (resp->error_code != ERR_NONE) {
                printf("rftp: server error %d\n", resp->error_code);
                return RFTP_ERR_SERVER;
            }
            g_seq_num++;
            return RFTP_SUCCESS;
        } else if (recv_buf[1] == MSG_ERROR) {
            printf("rftp: server returned error\n");
            return RFTP_ERR_SERVER;
        }
        
        retries++;
    }
    
    return RFTP_ERR_TIMEOUT;
}

/* Send batch request for multiple packets */
static int send_batch_request(uchar file_id, int *indices, int count) {
    struct batch_request req;
    
    req.header.version = RFTP_VERSION;
    req.header.msg_type = MSG_BATCH_REQ;
    req.header.seq_hi = (g_seq_num >> 8) & 0xFF;
    req.header.seq_lo = g_seq_num & 0xFF;
    req.file_id = file_id;
    req.count = (uchar)count;
    
    /* Fill in packet indices */
    for (int i = 0; i < count; i++) {
        uint_to_bytes(indices[i], &req.indices[i * 4]);
    }
    
    int msg_len = 6 + count * 4;
    if (send(g_local_port, RFTP_SERVER_IP, RFTP_PORT, 
             (char*)&req, msg_len) < 0) {
        return RFTP_ERR_SOCKET;
    }
    
    g_seq_num++;
    return RFTP_SUCCESS;
}

/* Process a single data response packet */
static int process_data_response(uchar *buf, int len, 
                                  char *file_buffer, uchar *received,
                                  int file_size) {
    if (len < 14) return -1;
    
    struct data_response_hdr *hdr = (struct data_response_hdr *)buf;
    
    if (hdr->header.msg_type != MSG_DATA_RESP) return -1;
    if (hdr->error_code != ERR_NONE) return -1;
    
    uint pkt_idx = bytes_to_uint(hdr->idx_b3, hdr->idx_b2, 
                                   hdr->idx_b1, hdr->idx_b0);
    ushort payload_len = bytes_to_ushort(hdr->len_hi, hdr->len_lo);
    ushort expected_csum = bytes_to_ushort(hdr->csum_hi, hdr->csum_lo);
    
    if (len < 14 + payload_len) return -1;
    
    uchar *payload = buf + 14;
    
    /* Verify checksum */
    ushort actual_csum = compute_checksum(payload, payload_len);
    if (actual_csum != expected_csum) {
        printf("rftp: checksum mismatch pkt %d\n", pkt_idx);
        return -1;  /* Will be retried */
    }
    
    /* Check for duplicate */
    if (received[pkt_idx]) {
        return 0;  /* Already have this one */
    }
    
    /* Store packet at correct offset */
    int offset = pkt_idx * RFTP_MAX_PAYLOAD;
    if (offset + payload_len > file_size) {
        payload_len = file_size - offset;
    }
    mymemcpy(file_buffer + offset, payload, payload_len);
    received[pkt_idx] = 1;
    
    return 1;  /* New packet stored */
}

/* Main file fetch function */
int rftp_fetch_file(uchar file_id, struct rftp_result *result) {
    struct metadata_response meta;
    uchar recv_buf[600];
    uint32 src_ip;
    uint16 src_port;
    int ret;
    
    /* Initialize result */
    mymemset(result, 0, sizeof(struct rftp_result));
    
    /* Initialize client if needed */
    if (!g_initialized) {
        ret = rftp_init();
        if (ret != RFTP_SUCCESS) return ret;
    }
    
    printf("rftp: requesting metadata for file %d...\n", file_id);
    
    /* Step 1: Get metadata */
    ret = request_metadata(file_id, &meta);
    if (ret != RFTP_SUCCESS) return ret;
    
    uint file_size = bytes_to_uint(meta.size_b3, meta.size_b2, 
                                     meta.size_b1, meta.size_b0);
    uint packet_count = bytes_to_uint(meta.pktcnt_b3, meta.pktcnt_b2, 
                                        meta.pktcnt_b1, meta.pktcnt_b0);
    
    printf("rftp: file size = %d bytes, packets = %d\n", file_size, packet_count);
    print_hex("rftp: expected SHA-256", meta.sha256_hash, 32);
    
    /* Step 2: Allocate buffers */
    char *file_buffer = malloc(file_size);
    if (!file_buffer) {
        printf("rftp: malloc failed for file buffer\n");
        return RFTP_ERR_MEMORY;
    }
    
    uchar *received = malloc(packet_count);
    if (!received) {
        free(file_buffer);
        printf("rftp: malloc failed for received array\n");
        return RFTP_ERR_MEMORY;
    }
    mymemset(received, 0, packet_count);
    
    /* Step 3: Request packets in batches */
    int total_received = 0;
    int full_retries = 0;
    
    while (total_received < packet_count && full_retries < 3) {
        /* Find missing packets and request in batches */
        int missing[RFTP_BATCH_SIZE];
        int missing_count = 0;
        
        for (int i = 0; i < packet_count; i++) {
            if (!received[i]) {
                missing[missing_count++] = i;
                
                if (missing_count == RFTP_BATCH_SIZE) {
                    /* Send batch request */
                    send_batch_request(file_id, missing, missing_count);
                    
                    /* Receive responses */
                    int recv_timeout = 0;
                    int batch_recv = 0;
                    
                    while (batch_recv < missing_count && recv_timeout < 5) {
                        int n = recv(g_local_port, &src_ip, &src_port, 
                                    (char*)recv_buf, sizeof(recv_buf));
                        if (n > 0) {
                            int r = process_data_response(recv_buf, n, 
                                        file_buffer, received, file_size);
                            if (r > 0) {
                                total_received++;
                                batch_recv++;
                                result->packets_received++;
                            }
                        } else {
                            /* Simple timeout counter */
                            for (long j = 0; j < RFTP_TIMEOUT_ITERS / 10; j++);
                            recv_timeout++;
                        }
                    }
                    
                    missing_count = 0;
                }
            }
        }
        
        /* Send remaining batch */
        if (missing_count > 0) {
            send_batch_request(file_id, missing, missing_count);
            
            int recv_timeout = 0;
            while (recv_timeout < 10) {
                int n = recv(g_local_port, &src_ip, &src_port, 
                            (char*)recv_buf, sizeof(recv_buf));
                if (n > 0) {
                    int r = process_data_response(recv_buf, n, 
                                file_buffer, received, file_size);
                    if (r > 0) {
                        total_received++;
                        result->packets_received++;
                    }
                } else {
                    for (long j = 0; j < RFTP_TIMEOUT_ITERS / 10; j++);
                    recv_timeout++;
                }
            }
        }
        
        /* Check if we got all packets */
        total_received = 0;
        for (int i = 0; i < packet_count; i++) {
            if (received[i]) total_received++;
        }
        
        if (total_received < packet_count) {
            printf("rftp: received %d/%d, retrying missing...\n", 
                   total_received, packet_count);
            result->retries++;
            full_retries++;
        }
    }
    
    printf("rftp: received all %d packets\n", packet_count);
    
    /* Step 4: Verify SHA-256 */
    printf("rftp: verifying SHA-256...\n");
    uchar computed_hash[32];
    sha256_hash((uchar*)file_buffer, file_size, computed_hash);
    
    print_hex("rftp: computed SHA-256", computed_hash, 32);
    
    if (mymemcmp(computed_hash, meta.sha256_hash, 32) != 0) {
        printf("rftp: SHA-256 MISMATCH! File corrupted.\n");
        free(file_buffer);
        free(received);
        return RFTP_ERR_INTEGRITY;
    }
    
    printf("rftp: SHA-256 verified OK!\n");
    
    /* Success - fill result */
    result->data = file_buffer;
    result->size = file_size;
    mymemcpy(result->sha256, computed_hash, 32);
    
    free(received);
    return RFTP_SUCCESS;
}

/* Wrapper: Fetch model weights */
char* fetch_model_weights(int *size_out) {
    struct rftp_result result;
    
    printf("\n=== Fetching Model Weights ===\n");
    int ret = rftp_fetch_file(FILE_ID_MODEL, &result);
    
    if (ret != RFTP_SUCCESS) {
        printf("fetch_model_weights: failed with error %d\n", ret);
        if (size_out) *size_out = 0;
        return 0;
    }
    
    if (size_out) *size_out = result.size;
    return result.data;
}

/* Wrapper: Fetch tokenizer */
char* fetch_tokenizer(int *size_out) {
    struct rftp_result result;
    
    printf("\n=== Fetching Tokenizer ===\n");
    int ret = rftp_fetch_file(FILE_ID_TOKENIZER, &result);
    
    if (ret != RFTP_SUCCESS) {
        printf("fetch_tokenizer: failed with error %d\n", ret);
        if (size_out) *size_out = 0;
        return 0;
    }
    
    if (size_out) *size_out = result.size;
    return result.data;
}

/* Test main function */
int main(int argc, char *argv[]) {
    printf("\n");
    printf("========================================\n");
    printf("  RFTP Client Test - xv6 LLM Weights\n");
    printf("========================================\n\n");
    
    /* Test 1: Fetch tokenizer (smaller file first) */
    int tok_size = 0;
    char *tokenizer = fetch_tokenizer(&tok_size);
    
    if (tokenizer) {
        printf("\nTokenizer fetch SUCCESS!\n");
        printf("  Size: %d bytes\n", tok_size);
        free(tokenizer);
    } else {
        printf("\nTokenizer fetch FAILED!\n");
    }
    
    /* Test 2: Fetch model weights */
    int model_size = 0;
    char *model = fetch_model_weights(&model_size);
    
    if (model) {
        printf("\nModel weights fetch SUCCESS!\n");
        printf("  Size: %d bytes\n", model_size);
        free(model);
    } else {
        printf("\nModel weights fetch FAILED!\n");
    }
    
    printf("\n========================================\n");
    printf("  Test Complete\n");
    printf("========================================\n");
    
    rftp_cleanup();
    return 0;
}