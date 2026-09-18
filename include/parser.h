#ifndef JAG_PARSER_H
#define JAG_PARSER_H
#include "ast.h"

typedef struct {
    StmtList *stmts;
    int had_error;
} ParseResult;

ParseResult parse_program(const char *source, const char *filename);

#endif
