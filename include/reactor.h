#ifndef JAG_REACTOR_H
#define JAG_REACTOR_H
#include "coro.h"

/* Portable readiness flags - callers outside the reactor backend itself
   (netutil.c, http.c, ws.c) use only these, never epoll/kqueue-specific
   constants directly, so the same calling code works against either
   backend (reactor_epoll.c on Linux, reactor_kqueue.c on macOS/BSD -
   selected by the Makefile per-platform; see DESIGN_DECISIONS.md). */
#define REACTOR_READABLE 0x1
#define REACTOR_WRITABLE 0x2

typedef void (*ReactorTimerFn)(void *arg);

/* Registers the fd for the given REACTOR_READABLE/REACTOR_WRITABLE
   event(s). When ready, the reactor resumes `co`. Only one registration
   per fd at a time. */
void reactor_watch_fd(int fd, unsigned events, Coro *co);
void reactor_unwatch_fd(int fd);

/* Schedules fn(arg) to run after delay_ms. If interval_ms > 0, reschedules
   itself every interval_ms after firing (live.every); otherwise fires once
   (live.after). Returns a timer id usable with reactor_clear_timer(). */
int reactor_add_timer(long delay_ms, long interval_ms, ReactorTimerFn fn, void *arg);
void reactor_clear_timer(int id);

/* True once at least one listening server or pending timer exists - i.e.
   there's a reason for the process to keep running instead of exiting. */
int reactor_has_work(void);

void reactor_mark_server_listening(void);

/* Runs the event loop until reactor_has_work() becomes false (Ctrl+C via
   SIGINT is the normal way this loop is left running). */
void reactor_run(void);

/* Yields the *currently running* coroutine until `fd` is ready for the given
   events, then resumes it. Must be called from inside a coroutine (i.e. from
   code running under coro_current() != NULL). */
void reactor_await_fd(int fd, unsigned events);

#endif
