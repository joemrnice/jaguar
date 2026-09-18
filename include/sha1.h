#ifndef JAG_SHA1_H
#define JAG_SHA1_H
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t state[5];
    uint64_t bitlen;
    unsigned char buffer[64];
    size_t buflen;
} SHA1_CTX;

void sha1_init(SHA1_CTX *ctx);
void sha1_update(SHA1_CTX *ctx, const unsigned char *data, size_t len);
void sha1_final(SHA1_CTX *ctx, unsigned char out[20]);
/* convenience: sha1 the whole buffer in one call */
void sha1(const unsigned char *data, size_t len, unsigned char out[20]);

#endif
