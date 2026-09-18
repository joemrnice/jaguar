/* macOS/BSD kqueue backend for reactor.h - the counterpart to
   reactor_epoll.c (Linux). See reactor_common.c for the OS-agnostic
   timer/bookkeeping logic shared by both.

   IMPORTANT: written to the documented kqueue(2)/kevent(2) API and
   modeled directly on the working epoll backend, but this project was
   developed and tested entirely on Linux - there is no macOS machine in
   that loop. Treat this file as reviewed-but-unverified until someone
   actually builds and runs it on macOS; see DESIGN_DECISIONS.md. */
#define _DARWIN_C_SOURCE
#include "reactor.h"
#include "reactor_internal.h"
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/event.h>
#include <errno.h>

#define MAX_EVENTS 64

static int g_kq = -1;

static void ensure_kq(void) {
    if (g_kq < 0) g_kq = kqueue();
}

void reactor_watch_fd(int fd, unsigned events, Coro *co) {
    ensure_kq();
    struct kevent changes[2];
    int n = 0;
    /* EV_ONESHOT: the kernel auto-removes the filter once it fires, which
       matches this reactor's one-shot-per-await semantics (see
       reactor_common.c's reactor_await_fd) without needing an explicit
       delete on the common path. */
    if (events & REACTOR_READABLE) {
        EV_SET(&changes[n], fd, EVFILT_READ, EV_ADD | EV_ONESHOT, 0, 0, (void *)co);
        n++;
    }
    if (events & REACTOR_WRITABLE) {
        EV_SET(&changes[n], fd, EVFILT_WRITE, EV_ADD | EV_ONESHOT, 0, 0, (void *)co);
        n++;
    }
    if (n > 0) kevent(g_kq, changes, n, NULL, 0, NULL);
}

void reactor_unwatch_fd(int fd) {
    if (g_kq < 0) return;
    /* Harmless if the filter already fired (EV_ONESHOT removed it) or was
       never registered - EV_DELETE on a missing filter just returns
       ENOENT, which we don't treat as an error here. */
    struct kevent changes[2];
    EV_SET(&changes[0], fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    EV_SET(&changes[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
    kevent(g_kq, changes, 2, NULL, 0, NULL);
}

void reactor_run(void) {
    ensure_kq();
    struct kevent events[MAX_EVENTS];
    while (reactor_has_work()) {
        long timeout_ms = reactor_next_timeout_ms();
        struct timespec ts;
        struct timespec *ts_ptr = NULL;
        if (timeout_ms >= 0) {
            ts.tv_sec = timeout_ms / 1000;
            ts.tv_nsec = (timeout_ms % 1000) * 1000000L;
            ts_ptr = &ts;
        }
        int n = kevent(g_kq, NULL, 0, events, MAX_EVENTS, ts_ptr);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("kevent");
            break;
        }
        for (int i = 0; i < n; i++) {
            Coro *co = (Coro *)events[i].udata;
            if (!co) continue;
            coro_resume(co);
            if (co->state == CORO_DONE) coro_destroy(co);
        }
        reactor_fire_due_timers();
    }
}
