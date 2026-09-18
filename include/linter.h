#ifndef JAG_LINTER_H
#define JAG_LINTER_H

#include "ast.h"
#include "diagnostics.h"

int lint_program(StmtList *program, const char *filename, DiagnosticBag *bag, int fix);

#endif
