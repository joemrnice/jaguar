#ifndef JAG_TYPECHECK_H
#define JAG_TYPECHECK_H
#include "ast.h"

/* Best-effort static checks (see DESIGN_DECISIONS.md for scope/limits):
   - fixed variables are never reassigned
   - await appears only inside async fun bodies (or is allowed loosely at
     top level, matching live-mode semantics)
   - var declarations with an initializer have a compatible literal type
     where that can be determined syntactically
   Returns 0 on success, number of diagnostics printed otherwise. */
int typecheck_program(StmtList *program, const char *filename);

#endif
