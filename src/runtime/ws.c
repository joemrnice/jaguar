#define _POSIX_C_SOURCE 200809L
#include "ws.h"
#include "native.h"
#include "netutil.h"
#include "reactor.h"
#include "coro.h"
#include "sha1.h"
#include "base64.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>
#include <stdint.h>

#define WS_GUID "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

typedef struct WsRoute { char *path; Value *handler; struct WsRoute *next; } WsRoute;
static WsRoute *g_ws_routes = NULL;
static int g_ws_listen_fd = -1;
static Interp *g_it = NULL;

void ws_server_on(Interp *it, int port) {
    g_it = it;
    g_ws_listen_fd = net_listen(port);
    if (g_ws_listen_fd < 0) fprintf(stderr, "jag: socket.on(%d) failed\n", port);
}

void ws_server_route(const char *path, Value *handler) {
    WsRoute *r = calloc(1, sizeof(WsRoute));
    r->path = strdup(path);
    r->handler = handler;
    r->next = g_ws_routes;
    g_ws_routes = r;
}

/* ---------------- Socket (ws) value method dispatch ---------------- */

static int ws_write_frame(int fd, int opcode, const char *payload, size_t len) {
    Buf out; buf_init(&out);
    unsigned char b0 = (unsigned char)(0x80 | (opcode & 0x0F));
    buf_append(&out, (char *)&b0, 1);
    if (len < 126) {
        unsigned char b1 = (unsigned char)len;
        buf_append(&out, (char *)&b1, 1);
    } else if (len < 65536) {
        unsigned char b1 = 126;
        buf_append(&out, (char *)&b1, 1);
        unsigned char ext[2] = { (unsigned char)((len >> 8) & 0xFF), (unsigned char)(len & 0xFF) };
        buf_append(&out, (char *)ext, 2);
    } else {
        unsigned char b1 = 127;
        buf_append(&out, (char *)&b1, 1);
        unsigned char ext[8];
        for (int i = 0; i < 8; i++) ext[i] = (unsigned char)((len >> (8 * (7 - i))) & 0xFF);
        buf_append(&out, (char *)ext, 8);
    }
    if (len > 0) buf_append(&out, payload, len);
    int rc = net_write_all(fd, out.data, out.len);
    buf_free(&out);
    return rc;
}

Value *ws_conn_call_method(Value *conn, const char *method, Value **args, int argc) {
    SocketConnNative *c = (SocketConnNative *)conn->as.native;
    if (strcmp(method, "on") == 0) {
        if (argc >= 2 && args[0]->kind == V_STRING) {
            if (strcmp(args[0]->as.string, "message") == 0) c->on_message = args[1];
            else if (strcmp(args[0]->as.string, "close") == 0) c->on_close = args[1];
        }
        return value_void();
    }
    if (strcmp(method, "send") == 0) {
        if (argc >= 1 && !c->closed) {
            char *s = value_to_display_string(args[0]);
            ws_write_frame(c->fd, 0x1, s, strlen(s));
            free(s);
        }
        return value_void();
    }
    if (strcmp(method, "close") == 0) {
        if (!c->closed) { ws_write_frame(c->fd, 0x8, "", 0); c->closed = 1; }
        return value_void();
    }
    return NULL;
}

/* ---------------- handshake ---------------- */

static long find_header_end(const char *data, size_t len) {
    for (size_t i = 0; i + 3 < len; i++)
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return (long)(i + 4);
    return -1;
}

static int extract_header(const char *data, long header_end, const char *name, char *out, size_t out_sz) {
    out[0] = '\0';
    const char *hp = strstr(data, "\r\n");
    if (!hp) return 0;
    hp += 2;
    const char *stop = data + header_end - 4;
    while (hp < stop) {
        const char *nl = strstr(hp, "\r\n");
        if (!nl || nl > stop) break;
        const char *colon = memchr(hp, ':', (size_t)(nl - hp));
        if (colon) {
            size_t klen = (size_t)(colon - hp);
            if (strncasecmp(hp, name, klen) == 0 && strlen(name) == klen) {
                const char *vstart = colon + 1;
                while (*vstart == ' ') vstart++;
                size_t vlen = (size_t)(nl - vstart);
                if (vlen >= out_sz) vlen = out_sz - 1;
                memcpy(out, vstart, vlen);
                out[vlen] = '\0';
                return 1;
            }
        }
        hp = nl + 2;
    }
    return 0;
}

/* ---------------- frame parsing ---------------- */

static int try_parse_frame(Buf *buf, int *fin, int *opcode, unsigned char **payload_out, size_t *payload_len_out, size_t *consumed_out) {
    if (buf->len < 2) return 0;
    unsigned char b0 = (unsigned char)buf->data[0], b1 = (unsigned char)buf->data[1];
    *fin = (b0 & 0x80) != 0;
    *opcode = b0 & 0x0F;
    int masked = (b1 & 0x80) != 0;
    uint64_t plen = b1 & 0x7F;
    size_t pos = 2;
    if (plen == 126) {
        if (buf->len < 4) return 0;
        plen = ((unsigned char)buf->data[2] << 8) | (unsigned char)buf->data[3];
        pos = 4;
    } else if (plen == 127) {
        if (buf->len < 10) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | (unsigned char)buf->data[2 + i];
        pos = 10;
    }
    unsigned char mask[4] = { 0, 0, 0, 0 };
    if (masked) {
        if (buf->len < pos + 4) return 0;
        memcpy(mask, buf->data + pos, 4);
        pos += 4;
    }
    if (buf->len < pos + plen) return 0;
    unsigned char *payload = malloc(plen > 0 ? plen : 1);
    memcpy(payload, buf->data + pos, plen);
    if (masked) for (uint64_t i = 0; i < plen; i++) payload[i] ^= mask[i % 4];
    *payload_out = payload;
    *payload_len_out = (size_t)plen;
    *consumed_out = pos + plen;
    return 1;
}

static void buf_consume_front(Buf *buf, size_t n) {
    if (n >= buf->len) { buf->len = 0; buf->data[0] = '\0'; return; }
    memmove(buf->data, buf->data + n, buf->len - n);
    buf->len -= n;
    buf->data[buf->len] = '\0';
}

/* ---------------- connection handling (coroutine) ---------------- */

typedef struct { int fd; } WsConnCtx;

static void ws_conn_entry(void *arg) {
    WsConnCtx *ctx = (WsConnCtx *)arg;
    int fd = ctx->fd;
    Buf b; buf_init(&b);
    long header_end = -1;
    for (;;) {
        header_end = find_header_end(b.data, b.len);
        if (header_end > 0) break;
        ssize_t n = net_read_some(fd, &b);
        if (n <= 0) { buf_free(&b); close(fd); free(ctx); return; }
        if (b.len > 65536) { buf_free(&b); close(fd); free(ctx); return; }
    }

    char reqline[512];
    const char *line_end = strstr(b.data, "\r\n");
    size_t rl_len = (size_t)(line_end - b.data);
    if (rl_len >= sizeof(reqline)) rl_len = sizeof(reqline) - 1;
    memcpy(reqline, b.data, rl_len);
    reqline[rl_len] = '\0';
    char method[16], path[512];
    sscanf(reqline, "%15s %511s", method, path);
    char *qmark = strchr(path, '?');
    if (qmark) *qmark = '\0';

    char ws_key[256];
    extract_header(b.data, header_end, "Sec-WebSocket-Key", ws_key, sizeof(ws_key));

    WsRoute *route = NULL;
    for (WsRoute *r = g_ws_routes; r; r = r->next) if (strcmp(r->path, path) == 0) { route = r; break; }

    if (!ws_key[0] || !route) {
        const char *resp = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n";
        net_write_all(fd, resp, strlen(resp));
        buf_free(&b);
        close(fd);
        free(ctx);
        return;
    }

    char concat[512];
    snprintf(concat, sizeof(concat), "%s%s", ws_key, WS_GUID);
    unsigned char digest[20];
    sha1((unsigned char *)concat, strlen(concat), digest);
    char *accept_key = base64_encode(digest, 20);

    char resp[512];
    snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key);
    free(accept_key);
    net_write_all(fd, resp, strlen(resp));

    /* Consume the HTTP handshake bytes; anything left over in `b` is the
       start of the first WS frame. */
    buf_consume_front(&b, (size_t)header_end);

    SocketConnNative *native = calloc(1, sizeof(SocketConnNative));
    native->fd = fd;
    native->it = g_it;
    Value *sock_val = native_wrap(NATIVE_SOCKET_CONN, native);

    Value *args[1] = { sock_val };
    interp_invoke_closure(g_it, route->handler, args, 1);

    for (;;) {
        int fin, opcode;
        unsigned char *payload;
        size_t payload_len, consumed;
        if (!try_parse_frame(&b, &fin, &opcode, &payload, &payload_len, &consumed)) {
            ssize_t n = net_read_some(fd, &b);
            if (n <= 0) break;
            continue;
        }
        (void)fin; /* fragmented-message reassembly is out of scope - see DESIGN_DECISIONS.md */
        if (opcode == 0x1 || opcode == 0x2) {
            if (native->on_message && !native->closed) {
                char *text = malloc(payload_len + 1);
                memcpy(text, payload, payload_len);
                text[payload_len] = '\0';
                Value *msg = value_string(text);
                free(text);
                Value *cb_args[1] = { msg };
                interp_invoke_closure(g_it, native->on_message, cb_args, 1);
            }
        } else if (opcode == 0x8) {
            if (!native->closed) { ws_write_frame(fd, 0x8, "", 0); native->closed = 1; }
            free(payload);
            buf_consume_front(&b, consumed);
            break;
        } else if (opcode == 0x9) {
            ws_write_frame(fd, 0xA, (char *)payload, payload_len); /* pong */
        }
        free(payload);
        buf_consume_front(&b, consumed);
    }

    if (native->on_close && !native->closed) {
        interp_invoke_closure(g_it, native->on_close, NULL, 0);
    }
    buf_free(&b);
    close(fd);
    free(ctx);
}

static void ws_accept_entry(void *arg) {
    (void)arg;
    for (;;) {
        reactor_await_fd(g_ws_listen_fd, REACTOR_READABLE);
        for (;;) {
            int cfd = accept(g_ws_listen_fd, NULL, NULL);
            if (cfd < 0) break;
            net_set_nonblocking(cfd);
            WsConnCtx *ctx = malloc(sizeof(WsConnCtx));
            ctx->fd = cfd;
            Coro *co = coro_create(ws_conn_entry, ctx, 0);
            coro_resume(co);
            if (co->state == CORO_DONE) coro_destroy(co);
        }
    }
}

void ws_server_listen(void) {
    if (g_ws_listen_fd < 0) return;
    reactor_mark_server_listening();
    Coro *co = coro_create(ws_accept_entry, NULL, 0);
    coro_resume(co);
}
