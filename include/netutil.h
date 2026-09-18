#ifndef JAG_NETUTIL_H
#define JAG_NETUTIL_H
#include <stddef.h>
#include <sys/types.h>

typedef struct { char *data; size_t len; size_t cap; } Buf;
void buf_init(Buf *b);
void buf_append(Buf *b, const char *data, size_t n);
void buf_free(Buf *b);

void net_set_nonblocking(int fd);

/* Creates, binds (SO_REUSEADDR), and listens a non-blocking TCP socket on
   `port`. Returns the fd, or -1 on failure (prints an error). */
int net_listen(int port);

/* Blocking (synchronous) TCP connect - used by the HTTP client, which is
   intentionally not coroutine-based; see DESIGN_DECISIONS.md. Returns fd
   or -1. */
int net_connect_blocking(const char *host, int port);

/* Coroutine-aware helpers: if called from inside a coroutine (coro_current()
   != NULL), they suspend via reactor_await_fd() on EAGAIN instead of
   blocking the process. If called outside a coroutine (e.g. the top-level
   script, or the HTTP client), they fall back to a plain blocking retry
   loop, which is a correct (if less efficient) way to do the same I/O. */
ssize_t net_read_some(int fd, Buf *into);           /* one recv (possibly after waiting); 0 = closed, -1 = error */
int net_write_all(int fd, const char *data, size_t len); /* 0 = ok, -1 = error */

#endif
