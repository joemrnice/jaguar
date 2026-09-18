/* Timer management and bookkeeping shared by both OS backends
   (reactor_epoll.c on Linux, reactor_kqueue.c on macOS/BSD) - neither
   touches an OS-specific API, so there's exactly one implementation of
   each rather than duplicating it per backend. */
#define _POSIX_C_SOURCE 200809L
#include "reactor.h"
#include "reactor_internal.h"
#include <stdlib.h>
#include <time.h>

typedef struct Timer {
    int id;
    long long due_ms;
    long interval_ms;
    ReactorTimerFn fn;
    void *arg;
    int active;
    struct Timer *next;
} Timer;

static Timer *g_timers = NULL;
static int g_next_timer_id = 1;
static int g_listening_count = 0;

static long long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int reactor_add_timer(long delay_ms, long interval_ms, ReactorTimerFn fn, void *arg) {
    Timer *t = calloc(1, sizeof(Timer));
    t->id = g_next_timer_id++;
    t->due_ms = now_ms() + delay_ms;
    t->interval_ms = interval_ms;
    t->fn = fn;
    t->arg = arg;
    t->active = 1;
    t->next = g_timers;
    g_timers = t;
    return t->id;
}

void reactor_clear_timer(int id) {
    for (Timer *t = g_timers; t; t = t->next)
        if (t->id == id) t->active = 0;
}

void reactor_mark_server_listening(void) { g_listening_count++; }

int reactor_has_work(void) {
    if (g_listening_count > 0) return 1;
    for (Timer *t = g_timers; t; t = t->next) if (t->active) return 1;
    return 0;
}

long reactor_next_timeout_ms(void) {
    long long soonest = -1;
    long long now = now_ms();
    for (Timer *t = g_timers; t; t = t->next) {
        if (!t->active) continue;
        long long remain = t->due_ms - now;
        if (remain < 0) remain = 0;
        if (soonest < 0 || remain < soonest) soonest = remain;
    }
    if (soonest < 0) return g_listening_count > 0 ? 1000 : -1;
    return (long)soonest;
}

void reactor_fire_due_timers(void) {
    long long now = now_ms();
    /* snapshot the list so a timer callback adding/removing timers mid-fire
       doesn't corrupt iteration */
    Timer *t = g_timers;
    while (t) {
        Timer *next = t->next;
        if (t->active && t->due_ms <= now) {
            t->fn(t->arg);
            if (t->interval_ms > 0 && t->active) {
                t->due_ms = now_ms() + t->interval_ms;
            } else {
                t->active = 0;
            }
        }
        t = next;
    }
}

void reactor_await_fd(int fd, unsigned events) {
    Coro *co = coro_current();
    if (!co) return; /* called outside a coroutine: nothing we can suspend */
    reactor_watch_fd(fd, events, co);
    coro_yield(NULL);
    reactor_unwatch_fd(fd);
}
