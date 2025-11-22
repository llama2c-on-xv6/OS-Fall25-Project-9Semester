#ifndef SHA256_H
#define SHA256_H

/* Compute SHA-256 hash.
 * data: pointer to bytes
 * len : number of bytes
 * hash: output buffer of 32 bytes
 */
void sha256_hash(const unsigned char *data, unsigned int len, unsigned char hash[32]);

#endif /* SHA256_H */
