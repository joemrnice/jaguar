#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include <stdlib.h>
#include <string.h>

Value *native_wrap(NativeTag tag, void *payload) {
    Value *v = value_new(V_NATIVE);
    ((NativeHeader *)payload)->tag = tag;
    v->as.native = payload;
    return v;
}

Value *task_done(Value *result) {
    TaskNative *t = calloc(1, sizeof(TaskNative));
    t->state = TASK_DONE;
    t->result = result;
    return native_wrap(NATIVE_TASK, t);
}

Value *task_error(const char *message) {
    TaskNative *t = calloc(1, sizeof(TaskNative));
    t->state = TASK_ERROR;
    t->error = strdup(message);
    return native_wrap(NATIVE_TASK, t);
}
