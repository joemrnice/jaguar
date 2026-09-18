#define _POSIX_C_SOURCE 200809L
#include "coro.h"
#include <stdlib.h>

static __thread Coro *g_current = NULL;
static __thread Coro *g_trampoline_target = NULL;

static void trampoline(void) {
    Coro *co = g_trampoline_target;
    co->entry(co->arg);
    co->state = CORO_DONE;
    /* swap back to caller for the last time; coro_resume()'s caller sees
       state == CORO_DONE and won't resume again. */
    swapcontext(&co->ctx, &co->caller_ctx);
}

Coro *coro_create(void (*entry)(void *arg), void *arg, size_t stack_size) {
    Coro *co = calloc(1, sizeof(Coro));
    co->stack_size = stack_size ? stack_size : (256 * 1024);
    co->stack = malloc(co->stack_size);
    co->entry = entry;
    co->arg = arg;
    co->state = CORO_READY;

    getcontext(&co->ctx);
    co->ctx.uc_stack.ss_sp = co->stack;
    co->ctx.uc_stack.ss_size = co->stack_size;
    co->ctx.uc_link = NULL;
    makecontext(&co->ctx, trampoline, 0);
    return co;
}

void coro_destroy(Coro *co) {
    if (!co) return;
    free(co->stack);
    free(co);
}

void coro_resume(Coro *co) {
    if (co->state == CORO_DONE) return;
    Coro *prev = g_current;
    g_current = co;
    co->state = CORO_RUNNING;
    g_trampoline_target = co; /* only consumed on the coroutine's first resume */
    swapcontext(&co->caller_ctx, &co->ctx);
    g_current = prev;
}

void coro_yield(void *value) {
    Coro *co = g_current;
    if (!co) return; /* not inside a coroutine - no-op */
    co->yield_value = value;
    co->state = CORO_READY;
    swapcontext(&co->ctx, &co->caller_ctx);
}

Coro *coro_current(void) { return g_current; }
