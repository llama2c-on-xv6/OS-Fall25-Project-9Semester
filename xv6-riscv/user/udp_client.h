#ifndef UDP_CLIENT_H
#define UDP_CLIENT_H

/*
 * udp_client.h
 * xv6 user-level Reliable File Transfer (RFTP) client interface.
 *
 * This header is written to match the xv6 user environment (types.h, user.h).
 * It assumes send()/recv()/bind() syscalls implemented in your kernel work
 * as in your net.c (send(sport,dst,dport,buf,len) and recv(dport,&src,&sport,buf,maxlen)).
 */

#include "kernel/types.h"   /* uint, uchar */
#include "user.h"    /* malloc/free/printf/exit prototypes */

/* QEMU guest <-> host mapping used in this project:
   - Host (visible from xv6) = 10.0.2.2  (RFTP_SERVER_IP)
   - QEMU forwards host UDP port X -> guest dport 2000 (NET_TESTS_PORT mapping).
   - We choose client source port inside xv6 = 2001 (so replies arrive on this bound port).
   Adjust these if you change host-forwarding configuration.
*/

//#define RFTP_SERVER_IP   ((10<<24) | (0<<16) | (2<<8) | 2)    /* 10.0.2.2 */
#define RFTP_SERVER_IP ((10<<24) | (0<<16) | (2<<8) | 2)  /* 10.0.2.2 */
#define RFTP_GUEST_DST_PORT 25999 // 2000   /* guest destination port (QEMU maps host->guest:2000) */
#define RFTP_CLIENT_PORT 2001      /* client local port inside xv6 to bind to and receive on */

/* Protocol message types (must match Python server) */
#define MSG_METADATA_REQ   1
#define MSG_METADATA_RESP  2
#define MSG_DATA_REQ       3
#define MSG_DATA_RESP      4

/* File IDs used between xv6 client and Python server */
#define FILE_ID_MODEL      1
#define FILE_ID_TOKENIZER  2

/* Return codes */
#define RFTP_SUCCESS         0
#define RFTP_ERR_SOCKET     -1
#define RFTP_ERR_MEMORY     -2
#define RFTP_ERR_SERVER     -3
#define RFTP_ERR_INTEGRITY  -4

/* Result container returned by rftp_fetch_file */
struct rftp_result {
    char *data;       /* malloc'ed buffer containing full file; caller must free() */
    uint size;        /* file size in bytes */
    unsigned char sha256[32]; /* server-provided SHA-256 (raw 32 bytes) */
};

/* Initialization / cleanup */
int rftp_init(void);      /* bind client port, do any setup; returns 0 on success */
int rftp_cleanup(void);   /* cleanup (optional) */

/* Fetch file by file id (FILE_ID_MODEL or FILE_ID_TOKENIZER).
   On success returns RFTP_SUCCESS and fills *res (res->data malloc'd).
   On failure returns negative error code; res fields are undefined on failure.
*/
int rftp_fetch_file(unsigned char file_id, struct rftp_result *res);

/* Convenience wrappers returning malloc'd buffer pointer (or NULL on failure).
   They set *size_out to filesize on success; caller must free() returned buffer.
*/
char *fetch_model_weights(int *size_out);
char *fetch_tokenizer(int *size_out);


void rftp_last_sha(unsigned char out[32]);

#endif /* UDP_CLIENT_H */
