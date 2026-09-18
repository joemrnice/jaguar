#define _POSIX_C_SOURCE 200809L
#include "linter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_stmt_lint(Stmt *s, const char *filename, DiagnosticBag *bag) {
    if (!s) return;
    if (s->kind == S_VARDECL) {
        if (s->as.vardecl.name && strlen(s->as.vardecl.name) == 1 && s->as.vardecl.name[0] == 'l') {
            diag_add(bag, DIAG_SEV_WARNING, "JAG001",
                     "Variable name 'l' is easily confused with '1' or 'I'",
                     filename, s->line, 1, s->line, 10, "linter",
                     "Consider choosing a clearer variable name");
        }
    } else if (s->kind == S_BLOCK) {
        for (StmtList *c = s->as.block; c; c = c->next) {
            check_stmt_lint(c->item, filename, bag);
        }
    } else if (s->kind == S_IF) {
        check_stmt_lint(s->as.if_stmt.then_b, filename, bag);
        if (s->as.if_stmt.else_b) check_stmt_lint(s->as.if_stmt.else_b, filename, bag);
    }
}

int lint_program(StmtList *program, const char *filename, DiagnosticBag *bag, int fix) {
    (void)fix;
    int initial_count = bag ? bag->count : 0;
    for (StmtList *c = program; c; c = c->next) {
        check_stmt_lint(c->item, filename, bag);
    }
    return (bag ? bag->count : 0) - initial_count;
}
