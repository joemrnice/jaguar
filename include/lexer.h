#ifndef JAG_LEXER_H
#define JAG_LEXER_H
#include "token.h"

typedef struct {
    const char *source;
    const char *cur;
    int line;
    const char *filename;
} Lexer;

void lexer_init(Lexer *lx, const char *source, const char *filename);
Token lexer_next(Lexer *lx);

#endif
