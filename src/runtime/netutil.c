#define _POSIX_C_SOURCE 200809L
#include "netutil.h"
#include "coro.h"
#include "reactor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <time.h>
#include <stdint.h>

void buf_init(Buf *b) { b->data = malloc(256); b->len = 0; b->cap = 256; b->data[0] = '\0'; }

void buf_append(Buf *b, const char *data, size_t n) {
    while (b->len + n + 1 > b->cap) { b->cap *= 2; b->data = realloc(b->data, b->cap); }
    memcpy(b->data + b->len, data, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_free(Buf *b) { free(b->data); b->data = NULL; b->len = b->cap = 0; }

void net_set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int net_listen(int port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons((uint16_t)port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        fprintf(stderr, "jag: bind() to port %d failed: %s\n", port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 128) != 0) { perror("listen"); close(fd); return -1; }
    net_set_nonblocking(fd);
    return fd;
}

int net_connect_blocking(const char *host, int port) {
    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0) return -1;
    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int yes = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

ssize_t net_read_some(int fd, Buf *into) {
    char tmp[8192];
    for (;;) {
        ssize_t n = recv(fd, tmp, sizeof(tmp), 0);
        if (n > 0) { buf_append(into, tmp, (size_t)n); return n; }
        if (n == 0) return 0; /* peer closed */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (coro_current()) {
                reactor_await_fd(fd, REACTOR_READABLE);
                continue;
            }
            /* not in a coroutine: plain blocking retry (fd may be
               non-blocking, so briefly sleep to avoid a busy spin) */
            struct timespec ts = { 0, 2 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            continue;
        }
        if (errno == EINTR) continue;
        return -1;
    }
}

int net_write_all(int fd, const char *data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (coro_current()) {
                reactor_await_fd(fd, REACTOR_WRITABLE);
                continue;
            }
            struct timespec ts = { 0, 2 * 1000 * 1000 };
            nanosleep(&ts, NULL);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        return -1;
    }
    return 0;
}
