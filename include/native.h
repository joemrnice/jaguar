#ifndef JAG_NATIVE_H
#define JAG_NATIVE_H
#include "value.h"
#include "interp.h"

/* Values of kind V_NATIVE point at one of these, tagged by `tag` so the
   interpreter's method dispatch (.then/.on/.submit/...) knows which kind
   of handle it's looking at. */
typedef enum {
    NATIVE_TASK,
    NATIVE_WORKER,
    NATIVE_WORKER_POOL,
    NATIVE_SOCKET_CONN,
    NATIVE_SUPER_REF
} NativeTag;

typedef struct { NativeTag tag; } NativeHeader;

typedef enum { TASK_DONE, TASK_ERROR } TaskResultState;

typedef struct {
    NativeHeader hdr;
    TaskResultState state;
    Value *result;   /* valid if state == TASK_DONE */
    char *error;      /* valid if state == TASK_ERROR */
} TaskNative;

typedef struct {
    NativeHeader hdr;
    struct WorkerHandle *handle;
} WorkerNative;

typedef struct {
    NativeHeader hdr;
    struct WorkerPool *pool;
} WorkerPoolNative;

/* A live WebSocket connection, passed as `ws: Socket` to a socket.route()
   handler. The handler registers callbacks via ws.on("message"/"close", fn);
   the connection's frame loop invokes them. */
typedef struct {
    NativeHeader hdr;
    int fd;
    Interp *it;
    Value *on_message;
    Value *on_close;
    int closed;
} SocketConnNative;

/* What `super` evaluates to inside a method/constructor: the parent class
   to resolve `super(...)`/`super.method(...)` against, bound to the same
   instance the currently-running method was called on. */
typedef struct {
    NativeHeader hdr;
    ClassInfo *parent_class;
    Value *this_instance;
} SuperRefNative;

Value *native_wrap(NativeTag tag, void *payload);
Value *task_done(Value *result);
Value *task_error(const char *message);

#endif
