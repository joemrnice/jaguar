#ifndef JAG_INTERP_H
#define JAG_INTERP_H
#include "ast.h"
#include "value.h"

typedef struct {
    Env *globals;
    int had_runtime_error;
    char *error_message;
    /* single-slot file/dir handles, matching the spec's file.on()/dir.on() singleton API */
    void *open_file;   /* FILE* */
    char *open_dir_path;
    void *open_dir;    /* DIR* */
} Interp;

void interp_init(Interp *it);
int interp_run(Interp *it, StmtList *program);

/* Invokes a zero-or-more-arg FuncValue with already-evaluated arguments,
   under a call_env rooted at `it`'s globals (NOT the closure's captured
   env - see DESIGN_DECISIONS.md's note on worker isolation). Used ONLY by
   the worker subsystem, which must not reach into the main thread's live
   local closures. */
Value *interp_invoke(Interp *it, Value *func, Value **argv, int argc);

/* Invokes a FuncValue preserving its normal lexical closure - i.e. the same
   semantics as calling it from Jaguar code. Used by the HTTP/WebSocket
   runtime to call route handlers and registered callbacks (e.g. a
   socket.route handler's `ws.on("message", fun(msg){ ws.send(...) })`
   depends on `ws` being visible via its closure). Safe to call from the
   main thread only (it does not take the worker isolation lock beyond what
   the caller already holds). */
Value *interp_invoke_closure(Interp *it, Value *func, Value **argv, int argc);

void interp_lock(void);
void interp_unlock(void);

#endif
