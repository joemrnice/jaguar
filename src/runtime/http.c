#define _POSIX_C_SOURCE 200809L
#include "http.h"
#include "native.h"
#include "netutil.h"
#include "reactor.h"
#include "coro.h"
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/socket.h>

/* ---------------- routes ---------------- */
typedef struct Route {
    char *method;
    char *pattern;
    Value *handler;
    struct Route *next;
} Route;

static Route *g_routes = NULL;
static int g_listen_fd = -1;
static Interp *g_it = NULL;

void http_server_on(Interp *it, int port) {
    g_it = it;
    g_listen_fd = net_listen(port);
    if (g_listen_fd < 0) fprintf(stderr, "jag: server.on(%d) failed\n", port);
}

void http_server_route(const char *method, const char *pattern, Value *handler) {
    Route *r = calloc(1, sizeof(Route));
    r->method = strdup(method);
    for (char *c = r->method; *c; c++) *c = (char)toupper((unsigned char)*c);
    r->pattern = strdup(pattern);
    r->handler = handler;
    r->next = g_routes;
    g_routes = r;
}

/* ---------------- request/response value helpers ---------------- */

Value *http_make_response(void) {
    Value *res = value_data_empty();
    value_data_set(res, "__is_response__", value_bool(1));
    value_data_set(res, "__status", value_num(200));
    value_data_set(res, "__body", value_string(""));
    value_data_set(res, "__headers", value_data_empty());
    return res;
}

int http_is_response_object(Value *v) {
    return v->kind == V_DATA && value_data_get(v, "__is_response__") != NULL;
}

Value *http_response_call_method(Value *obj, const char *method, Value **args, int argc) {
    if (strcmp(method, "send") == 0) {
        Value *body = argc > 0 ? args[0] : value_string("");
        char *s = value_to_display_string(body);
        value_data_set(obj, "__body", value_string(s));
        free(s);
        return obj;
    }
    if (strcmp(method, "json") == 0) {
        Value *data = argc > 0 ? args[0] : value_data_empty();
        char *s = json_stringify(data);
        value_data_set(obj, "__body", value_string(s));
        free(s);
        Value *headers = value_data_get(obj, "__headers");
        value_data_set(headers, "Content-Type", value_string("application/json"));
        return obj;
    }
    if (strcmp(method, "status") == 0) {
        long long code = argc > 0 ? (long long)value_as_number(args[0]) : 200;
        value_data_set(obj, "__status", value_num(code));
        return obj;
    }
    if (strcmp(method, "header") == 0) {
        if (argc >= 2 && args[0]->kind == V_STRING) {
            Value *headers = value_data_get(obj, "__headers");
            char *v = value_to_display_string(args[1]);
            value_data_set(headers, args[0]->as.string, value_string(v));
            free(v);
        }
        return obj;
    }
    return NULL;
}

static const char *status_text(long long code) {
    switch (code) {
        case 200: return "OK"; case 201: return "Created"; case 204: return "No Content";
        case 301: return "Moved Permanently"; case 302: return "Found";
        case 400: return "Bad Request"; case 401: return "Unauthorized";
        case 403: return "Forbidden"; case 404: return "Not Found";
        case 405: return "Method Not Allowed"; case 500: return "Internal Server Error";
        case 502: return "Bad Gateway"; case 503: return "Service Unavailable";
        default: return "OK";
    }
}

/* ---------------- wire-format parsing ---------------- */

static long find_header_end(const char *data, size_t len) {
    for (size_t i = 0; i + 3 < len; i++)
        if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
            return (long)(i + 4);
    return -1;
}

typedef struct { char key[128]; char value[512]; } Header;

typedef struct {
    char method[16];
    char path[1024];
    char query[1024];
    Header headers[48];
    int header_count;
    const char *body;
    size_t body_len;
} ParsedRequest;

static void get_header(ParsedRequest *req, const char *name, char *out, size_t out_sz) {
    out[0] = '\0';
    for (int i = 0; i < req->header_count; i++)
        if (strcasecmp(req->headers[i].key, name) == 0) { snprintf(out, out_sz, "%s", req->headers[i].value); return; }
}

static int parse_request(Buf *b, long header_end, ParsedRequest *req) {
    memset(req, 0, sizeof(*req));
    const char *p = b->data;
    /* request line: METHOD SP PATH SP HTTP/1.1 CRLF */
    const char *line_end = strstr(p, "\r\n");
    if (!line_end) return 0;
    char reqline[2048];
    size_t rl_len = (size_t)(line_end - p);
    if (rl_len >= sizeof(reqline)) rl_len = sizeof(reqline) - 1;
    memcpy(reqline, p, rl_len);
    reqline[rl_len] = '\0';

    char full_path[1024];
    if (sscanf(reqline, "%15s %1023s", req->method, full_path) != 2) return 0;
    char *qmark = strchr(full_path, '?');
    if (qmark) {
        snprintf(req->query, sizeof(req->query), "%s", qmark + 1);
        *qmark = '\0';
    }
    snprintf(req->path, sizeof(req->path), "%s", full_path);

    const char *hp = line_end + 2;
    const char *headers_stop = p + header_end - 4; /* points at the blank-line CRLF */
    while (hp < headers_stop) {
        const char *nl = strstr(hp, "\r\n");
        if (!nl || nl > headers_stop) break;
        const char *colon = memchr(hp, ':', (size_t)(nl - hp));
        if (colon && req->header_count < 48) {
            Header *h = &req->headers[req->header_count];
            size_t klen = (size_t)(colon - hp);
            if (klen >= sizeof(h->key)) klen = sizeof(h->key) - 1;
            memcpy(h->key, hp, klen); h->key[klen] = '\0';
            const char *vstart = colon + 1;
            while (*vstart == ' ') vstart++;
            size_t vlen = (size_t)(nl - vstart);
            if (vlen >= sizeof(h->value)) vlen = sizeof(h->value) - 1;
            memcpy(h->value, vstart, vlen); h->value[vlen] = '\0';
            req->header_count++;
        }
        hp = nl + 2;
    }
    req->body = b->data + header_end;
    req->body_len = b->len - (size_t)header_end;
    return 1;
}

static long content_length_of(Buf *b, long header_end) {
    ParsedRequest tmp;
    parse_request(b, header_end, &tmp);
    char cl[64];
    get_header(&tmp, "Content-Length", cl, sizeof(cl));
    return cl[0] ? atol(cl) : 0;
}

/* route matching: supports literal segments and {param} segments */
static Route *match_route(const char *method, const char *path, Value *params_out) {
    for (Route *r = g_routes; r; r = r->next) {
        if (strcasecmp(r->method, method) != 0) continue;
        char pat[1024], pth[1024];
        snprintf(pat, sizeof(pat), "%s", r->pattern);
        snprintf(pth, sizeof(pth), "%s", path);
        char *pat_save, *pth_save;
        char *pat_seg = strtok_r(pat, "/", &pat_save);
        char *pth_seg = strtok_r(pth, "/", &pth_save);
        int ok = 1;
        Value *local_params = value_data_empty();
        while (pat_seg || pth_seg) {
            if (!pat_seg || !pth_seg) { ok = 0; break; }
            size_t plen = strlen(pat_seg);
            if (plen >= 2 && pat_seg[0] == '{' && pat_seg[plen - 1] == '}') {
                char name[128];
                size_t nlen = plen - 2;
                if (nlen >= sizeof(name)) nlen = sizeof(name) - 1;
                memcpy(name, pat_seg + 1, nlen);
                name[nlen] = '\0';
                value_data_set(local_params, name, value_string(pth_seg));
            } else if (strcmp(pat_seg, pth_seg) != 0) {
                ok = 0; break;
            }
            pat_seg = strtok_r(NULL, "/", &pat_save);
            pth_seg = strtok_r(NULL, "/", &pth_save);
        }
        if (ok) {
            for (DataNode *c = local_params->as.data; c; c = c->next) value_data_set(params_out, c->key, c->val);
            return r;
        }
    }
    return NULL;
}

static Value *parse_query_string(const char *q) {
    Value *out = value_data_empty();
    if (!q || !*q) return out;
    char buf[1024];
    snprintf(buf, sizeof(buf), "%s", q);
    char *save;
    for (char *pair = strtok_r(buf, "&", &save); pair; pair = strtok_r(NULL, "&", &save)) {
        char *eq = strchr(pair, '=');
        if (eq) { *eq = '\0'; value_data_set(out, pair, value_string(eq + 1)); }
        else value_data_set(out, pair, value_string(""));
    }
    return out;
}

/* ---------------- connection handling (coroutine) ---------------- */

typedef struct { int fd; } ConnCtx;

static void http_conn_entry(void *arg) {
    ConnCtx *cc = (ConnCtx *)arg;
    Buf b; buf_init(&b);
    long header_end = -1;
    long content_len = 0;
    for (;;) {
        header_end = find_header_end(b.data, b.len);
        if (header_end > 0) {
            content_len = content_length_of(&b, header_end);
            if (b.len >= (size_t)header_end + (size_t)content_len) break;
        }
        ssize_t n = net_read_some(cc->fd, &b);
        if (n <= 0) { buf_free(&b); close(cc->fd); free(cc); return; }
        if (b.len > 4 * 1024 * 1024) { buf_free(&b); close(cc->fd); free(cc); return; } /* guard */
    }

    ParsedRequest req;
    parse_request(&b, header_end, &req);

    Value *params = value_data_empty();
    Route *route = match_route(req.method, req.path, params);

    Value *req_val = value_data_empty();
    value_data_set(req_val, "method", value_string(req.method));
    value_data_set(req_val, "path", value_string(req.path));
    value_data_set(req_val, "params", params);
    value_data_set(req_val, "query", parse_query_string(req.query));
    Value *headers_val = value_data_empty();
    for (int i = 0; i < req.header_count; i++) value_data_set(headers_val, req.headers[i].key, value_string(req.headers[i].value));
    value_data_set(req_val, "headers", headers_val);
    char *body_copy = malloc(req.body_len + 1);
    memcpy(body_copy, req.body, req.body_len);
    body_copy[req.body_len] = '\0';
    Value *body_val = value_string(body_copy);
    free(body_copy);
    value_data_set(req_val, "body", body_val);

    Value *res_val = http_make_response();

    if (route) {
        Value *args[2] = { req_val, res_val };
        interp_invoke_closure(g_it, route->handler, args, 2);
    } else {
        Value *sargs[1] = { value_num(404) };
        http_response_call_method(res_val, "status", sargs, 1);
        Value *bargs[1] = { value_string("Not Found") };
        http_response_call_method(res_val, "send", bargs, 1);
    }

    long long status = (long long)value_as_number(value_data_get(res_val, "__status"));
    Value *body = value_data_get(res_val, "__body");
    const char *body_s = body->kind == V_STRING ? body->as.string : "";
    Value *hdrs = value_data_get(res_val, "__headers");

    Buf out; buf_init(&out);
    char line[256];
    snprintf(line, sizeof(line), "HTTP/1.1 %lld %s\r\n", status, status_text(status));
    buf_append(&out, line, strlen(line));
    for (DataNode *c = hdrs->as.data; c; c = c->next) {
        char *v = value_to_display_string(c->val);
        snprintf(line, sizeof(line), "%s: %s\r\n", c->key, v);
        free(v);
        buf_append(&out, line, strlen(line));
    }
    snprintf(line, sizeof(line), "Content-Length: %zu\r\n", strlen(body_s));
    buf_append(&out, line, strlen(line));
    buf_append(&out, "Connection: close\r\n\r\n", strlen("Connection: close\r\n\r\n"));
    buf_append(&out, body_s, strlen(body_s));

    net_write_all(cc->fd, out.data, out.len);
    buf_free(&out);
    buf_free(&b);
    close(cc->fd);
    free(cc);
}

static void http_accept_entry(void *arg) {
    (void)arg;
    for (;;) {
        reactor_await_fd(g_listen_fd, REACTOR_READABLE);
        for (;;) {
            int cfd = accept(g_listen_fd, NULL, NULL);
            if (cfd < 0) break;
            net_set_nonblocking(cfd);
            ConnCtx *cc = malloc(sizeof(ConnCtx));
            cc->fd = cfd;
            Coro *co = coro_create(http_conn_entry, cc, 0);
            coro_resume(co);
            if (co->state == CORO_DONE) coro_destroy(co);
        }
    }
}

void http_server_listen(void) {
    if (g_listen_fd < 0) return;
    reactor_mark_server_listening();
    Coro *co = coro_create(http_accept_entry, NULL, 0);
    coro_resume(co); /* runs until its first await, then returns control here */
}

/* ---------------- client (blocking) ---------------- */

static int parse_url(const char *url, char *scheme, char *host, int *port, char *path) {
    scheme[0] = host[0] = path[0] = '\0';
    *port = 80;
    const char *p = strstr(url, "://");
    if (!p) return 0;
    size_t slen = (size_t)(p - url);
    if (slen >= 16) slen = 15;
    memcpy(scheme, url, slen); scheme[slen] = '\0';
    p += 3;
    const char *slash = strchr(p, '/');
    const char *hostend = slash ? slash : p + strlen(p);
    char hostport[256];
    size_t hlen = (size_t)(hostend - p);
    if (hlen >= sizeof(hostport)) hlen = sizeof(hostport) - 1;
    memcpy(hostport, p, hlen); hostport[hlen] = '\0';
    char *colon = strchr(hostport, ':');
    if (colon) { *colon = '\0'; *port = atoi(colon + 1); }
    snprintf(host, 256, "%s", hostport);
    if (strcasecmp(scheme, "https") == 0 && colon == NULL) *port = 443;
    snprintf(path, 1024, "%s", slash ? slash : "/");
    return 1;
}

static Value *do_request(const char *method, const char *url, Value *body) {
    char scheme[16], host[256], path[1024];
    int port;
    if (!parse_url(url, scheme, host, &port, path)) return task_error("invalid URL");
    if (strcasecmp(scheme, "https") == 0)
        return task_error("https:// is not available in this build: no TLS library is linked "
                           "(see DESIGN_DECISIONS.md). Plain http:// works.");

    int fd = net_connect_blocking(host, port);
    if (fd < 0) return task_error("could not connect");

    char *body_str = NULL;
    if (body) { body_str = json_stringify(body); }
    size_t body_len = body_str ? strlen(body_str) : 0;

    Buf req; buf_init(&req);
    char line[1024];
    snprintf(line, sizeof(line), "%s %s HTTP/1.1\r\n", method, path);
    buf_append(&req, line, strlen(line));
    snprintf(line, sizeof(line), "Host: %s\r\n", host);
    buf_append(&req, line, strlen(line));
    buf_append(&req, "Connection: close\r\n", strlen("Connection: close\r\n"));
    if (body_str) {
        buf_append(&req, "Content-Type: application/json\r\n", strlen("Content-Type: application/json\r\n"));
        snprintf(line, sizeof(line), "Content-Length: %zu\r\n", body_len);
        buf_append(&req, line, strlen(line));
    }
    buf_append(&req, "\r\n", strlen("\r\n"));
    if (body_str) buf_append(&req, body_str, body_len);
    free(body_str);

    if (net_write_all(fd, req.data, req.len) != 0) { buf_free(&req); close(fd); return task_error("write failed"); }
    buf_free(&req);

    Buf resp; buf_init(&resp);
    long header_end = -1;
    long content_len = 0;
    for (;;) {
        header_end = find_header_end(resp.data, resp.len);
        if (header_end > 0) {
            content_len = content_length_of(&resp, header_end);
            if (content_len == 0 || resp.len >= (size_t)header_end + (size_t)content_len) break;
        }
        ssize_t n = net_read_some(fd, &resp);
        if (n <= 0) break; /* connection closed - treat whatever we have as final */
        if (resp.len > 16 * 1024 * 1024) break;
    }
    close(fd);

    if (header_end < 0) { buf_free(&resp); return task_error("malformed response"); }

    ParsedRequest fake; /* reuse the header parser structurally (status line differs, so parse manually below) */
    (void)fake;
    char status_line[256];
    const char *nl = strstr(resp.data, "\r\n");
    size_t sl_len = nl ? (size_t)(nl - resp.data) : 0;
    if (sl_len >= sizeof(status_line)) sl_len = sizeof(status_line) - 1;
    memcpy(status_line, resp.data, sl_len);
    status_line[sl_len] = '\0';
    int status = 0;
    char httpver[32];
    sscanf(status_line, "%31s %d", httpver, &status);

    Value *result = value_data_empty();
    value_data_set(result, "status", value_num(status));
    Value *headers_val = value_data_empty();
    const char *hp = nl + 2;
    const char *headers_stop = resp.data + header_end - 4;
    while (hp < headers_stop) {
        const char *hnl = strstr(hp, "\r\n");
        if (!hnl || hnl > headers_stop) break;
        const char *colon = memchr(hp, ':', (size_t)(hnl - hp));
        if (colon) {
            char key[128], val[512];
            size_t klen = (size_t)(colon - hp); if (klen >= sizeof(key)) klen = sizeof(key) - 1;
            memcpy(key, hp, klen); key[klen] = '\0';
            const char *vstart = colon + 1;
            while (*vstart == ' ') vstart++;
            size_t vlen = (size_t)(hnl - vstart); if (vlen >= sizeof(val)) vlen = sizeof(val) - 1;
            memcpy(val, vstart, vlen); val[vlen] = '\0';
            value_data_set(headers_val, key, value_string(val));
        }
        hp = hnl + 2;
    }
    value_data_set(result, "headers", headers_val);
    size_t actual_body_len = resp.len > (size_t)header_end ? resp.len - (size_t)header_end : 0;
    char *body_copy = malloc(actual_body_len + 1);
    memcpy(body_copy, resp.data + header_end, actual_body_len);
    body_copy[actual_body_len] = '\0';
    value_data_set(result, "body", value_string(body_copy));
    free(body_copy);
    buf_free(&resp);

    return task_done(result);
}

Value *http_client_get(const char *url) { return do_request("GET", url, NULL); }
Value *http_client_post(const char *url, Value *body) { return do_request("POST", url, body); }
