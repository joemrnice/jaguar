#define _POSIX_C_SOURCE 200809L
#include "ast.h"
#include <stdlib.h>
#include <string.h>

TypeInfo *type_new(JagType kind) {
    TypeInfo *t = calloc(1, sizeof(TypeInfo));
    t->kind = kind;
    return t;
}

Expr *expr_new(ExprKind kind, int line) {
    Expr *e = calloc(1, sizeof(Expr));
    e->kind = kind;
    e->line = line;
    return e;
}

Stmt *stmt_new(StmtKind kind, int line) {
    Stmt *s = calloc(1, sizeof(Stmt));
    s->kind = kind;
    s->line = line;
    return s;
}
