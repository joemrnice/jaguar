#ifndef JAG_CORO_H
#define JAG_CORO_H
#include <ucontext.h>

typedef enum { CORO_READY, CORO_RUNNING, CORO_DONE } CoroState;

typedef struct Coro {
    ucontext_t ctx;
    ucontext_t caller_ctx;   /* where to swap back to on yield/finish */
    unsigned char *stack;
    size_t stack_size;
    CoroState state;
    void (*entry)(void *arg);
    void *arg;
    void *yield_value;       /* set by coro_yield(), read after coro_resume() returns */
} Coro;

Coro *coro_create(void (*entry)(void *arg), void *arg, size_t stack_size);
void coro_destroy(Coro *co);

/* Switches into the coroutine. Returns when the coroutine yields or finishes.
   Call from the reactor's main context only. */
void coro_resume(Coro *co);

/* Switches back to whoever called coro_resume(), carrying `value` out.
   Call only from inside the running coroutine. */
void coro_yield(void *value);

/* The coroutine currently executing on this OS thread, or NULL if none
   (i.e. we're in the reactor's main context). */
Coro *coro_current(void);

#endif
