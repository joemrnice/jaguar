/* Linux epoll backend for reactor.h. See reactor_kqueue.c for the
   macOS/BSD backend and reactor_common.c for the OS-agnostic timer/
   bookkeeping logic shared by both - the Makefile picks exactly one of
   reactor_epoll.c/reactor_kqueue.c to compile, per-platform. Both
   translate the portable REACTOR_READABLE/REACTOR_WRITABLE flags from
   reactor.h to their OS's native constants; nothing outside this file
   (or reactor_kqueue.c) should need to know which backend is in use. */
#define _POSIX_C_SOURCE 200809L
#include "reactor.h"
#include "reactor_internal.h"
#include <stdio.h>
#include <string.h>
#include <sys/epoll.h>
#include <errno.h>

#define MAX_EVENTS 64

static int g_epoll_fd = -1;

static void ensure_epoll(void) {
    if (g_epoll_fd < 0) g_epoll_fd = epoll_create1(0);
}

void reactor_watch_fd(int fd, unsigned events, Coro *co) {
    ensure_epoll();
    unsigned epoll_events = 0;
    if (events & REACTOR_READABLE) epoll_events |= EPOLLIN;
    if (events & REACTOR_WRITABLE) epoll_events |= EPOLLOUT;
    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = epoll_events;
    ev.data.ptr = co;
    if (epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, fd, &ev) != 0) {
        if (errno == EEXIST) epoll_ctl(g_epoll_fd, EPOLL_CTL_MOD, fd, &ev);
    }
}

void reactor_unwatch_fd(int fd) {
    if (g_epoll_fd < 0) return;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

void reactor_run(void) {
    ensure_epoll();
    struct epoll_event events[MAX_EVENTS];
    while (reactor_has_work()) {
        long timeout = reactor_next_timeout_ms();
        int n = epoll_wait(g_epoll_fd, events, MAX_EVENTS, timeout);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("epoll_wait");
            break;
        }
        for (int i = 0; i < n; i++) {
            Coro *co = (Coro *)events[i].data.ptr;
            if (!co) continue;
            coro_resume(co);
            if (co->state == CORO_DONE) coro_destroy(co);
        }
        reactor_fire_due_timers();
    }
}
