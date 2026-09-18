#include "base64.h"
#include <stdlib.h>
#include <stdint.h>

static const char TABLE[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

char *base64_encode(const unsigned char *data, size_t len) {
    size_t out_len = 4 * ((len + 2) / 3);
    char *out = malloc(out_len + 1);
    size_t i = 0, j = 0;
    while (i + 2 < len) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8) | data[i + 2];
        out[j++] = TABLE[(n >> 18) & 0x3F];
        out[j++] = TABLE[(n >> 12) & 0x3F];
        out[j++] = TABLE[(n >> 6) & 0x3F];
        out[j++] = TABLE[n & 0x3F];
        i += 3;
    }
    size_t rem = len - i;
    if (rem == 1) {
        uint32_t n = (uint32_t)data[i] << 16;
        out[j++] = TABLE[(n >> 18) & 0x3F];
        out[j++] = TABLE[(n >> 12) & 0x3F];
        out[j++] = '=';
        out[j++] = '=';
    } else if (rem == 2) {
        uint32_t n = ((uint32_t)data[i] << 16) | ((uint32_t)data[i + 1] << 8);
        out[j++] = TABLE[(n >> 18) & 0x3F];
        out[j++] = TABLE[(n >> 12) & 0x3F];
        out[j++] = TABLE[(n >> 6) & 0x3F];
        out[j++] = '=';
    }
    out[j] = '\0';
    return out;
}
