#ifndef JAG_BASE64_H
#define JAG_BASE64_H
#include <stddef.h>

/* Returns a malloc'd, NUL-terminated base64 string. Caller frees. */
char *base64_encode(const unsigned char *data, size_t len);

#endif
