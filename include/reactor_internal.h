#ifndef JAG_REACTOR_INTERNAL_H
#define JAG_REACTOR_INTERNAL_H

/* Not part of the public reactor.h API - shared only between
   reactor_common.c (timers, bookkeeping, reactor_await_fd) and whichever
   OS-specific backend (reactor_epoll.c or reactor_kqueue.c) is compiled
   in, which each need to drive their poll/wait call with the next timer
   deadline and fire due timers after it returns. */

/* Milliseconds until the next timer is due, or -1 to block indefinitely
   (no timers pending) - a listening server with no timers still returns
   a finite value so reactor_has_work() gets re-checked periodically. */
long reactor_next_timeout_ms(void);

void reactor_fire_due_timers(void);

#endif
