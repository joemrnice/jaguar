#define _POSIX_C_SOURCE 200809L
#include "interp.h"
#include "json.h"
#include "native.h"
#include "http.h"
#include "ws.h"
#include "worker.h"
#include "reactor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ctype.h>
#include <stdarg.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include "token.h"

typedef enum { SIG_NONE, SIG_RETURN, SIG_THROW } Signal;
typedef struct { Signal sig; Value *val; } ExecResult;

static ExecResult exec_block(Interp *it, StmtList *list, Env *env);
static ExecResult exec_stmt(Interp *it, Stmt *s, Env *env);
static Value *eval_expr(Interp *it, Expr *e, Env *env);
static Value *call_value(Interp *it, Value *callee_fn, ExprList *args, Env *env, int *threw, int line);
static void hoist_classes(Interp *it, StmtList *list, Env *env);
static int find_method(ClassInfo *start, const char *name, ClassInfo **out_defining, Stmt **out_stmt);
static Value **eval_args(Interp *it, ExprList *args, Env *env, int *out_argc);
static Value *invoke_method(Interp *it, ClassInfo *defining_class, Stmt *method_stmt, Value *this_instance, Value **argv, int argc);
static Value *construct_instance(Interp *it, ClassInfo *ci, ExprList *args, Env *env, int line);

static ExecResult res_none(void) { ExecResult r = { SIG_NONE, NULL }; return r; }
static ExecResult res_return(Value *v) { ExecResult r = { SIG_RETURN, v }; return r; }

/* Sets the pending-error state without printing; try/catch may consume it
   silently, so only interp_run() prints it if it escapes uncaught. */
static void rt_error(Interp *it, int line, const char *fmt, ...) {
    char buf[900];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char full[1024];
    snprintf(full, sizeof(full), "line %d: %s", line, buf);
    it->had_runtime_error = 1;
    if (it->error_message) free(it->error_message);
    it->error_message = strdup(full);
}

void interp_init(Interp *it) {
    it->globals = env_new(NULL);
    it->had_runtime_error = 0;
    it->error_message = NULL;
    it->open_file = NULL;
    it->open_dir_path = NULL;
    it->open_dir = NULL;
}

/* ---------------- string interpolation ---------------- */
static Value *eval_string_interp(Interp *it, Expr *e, Env *env) {
    size_t cap = 256, len = 0;
    char *out = malloc(cap);
    out[0] = '\0';
    for (StrPart *p = e->as.str_parts; p; p = p->next) {
        char *piece;
        if (p->is_expr) {
            Value *v = eval_expr(it, p->expr, env);
            if (it->had_runtime_error) { free(out); return value_string(""); }
            piece = value_to_display_string(v);
        } else {
            piece = strdup(p->literal);
        }
        size_t need = strlen(piece) + 1;
        while (len + need > cap) { cap *= 2; out = realloc(out, cap); }
        strcat(out, piece);
        len += strlen(piece);
        free(piece);
    }
    Value *v = value_string(out);
    free(out);
    return v;
}

/* ---------------- list/data builtin methods (json.parse/stringify now
   live in json.c, shared with the HTTP runtime) ---------------- */
static Value *call_list_method(Interp *it, Value *list, const char *name, ExprList *args, Env *env, int line) {
    if (strcmp(name, "append") == 0) {
        if (args) value_list_append(list, eval_expr(it, args->item, env));
        return value_void();
    }
    if (strcmp(name, "insert") == 0) {
        if (args) value_list_insert_front(list, eval_expr(it, args->item, env));
        return value_void();
    }
    if (strcmp(name, "delete") == 0) {
        if (!args) return value_void();
        Value *idxv = eval_expr(it, args->item, env);
        long long idx = (long long)value_as_number(idxv);
        ListNode *prev = NULL, *cur = list->as.list;
        long long i = 0;
        while (cur) {
            if (i == idx) {
                if (prev) prev->next = cur->next; else list->as.list = cur->next;
                free(cur);
                break;
            }
            prev = cur; cur = cur->next; i++;
        }
        return value_void();
    }
    if (strcmp(name, "sort") == 0) {
        int n = value_list_length(list);
        Value **arr = malloc(sizeof(Value*) * (n > 0 ? n : 1));
        int i = 0;
        for (ListNode *c = list->as.list; c; c = c->next) arr[i++] = c->item;
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++) {
                int gt;
                if (arr[a]->kind == V_STRING && arr[b]->kind == V_STRING)
                    gt = strcmp(arr[a]->as.string, arr[b]->as.string) > 0;
                else
                    gt = value_as_number(arr[a]) > value_as_number(arr[b]);
                if (gt) { Value *t = arr[a]; arr[a] = arr[b]; arr[b] = t; }
            }
        ListNode *node = NULL, *newhead = NULL, *tail = NULL;
        for (int k = 0; k < n; k++) {
            node = calloc(1, sizeof(ListNode));
            node->item = arr[k];
            if (!newhead) newhead = tail = node; else { tail->next = node; tail = node; }
        }
        list->as.list = newhead;
        free(arr);
        return value_void();
    }
    if (strcmp(name, "concat") == 0) {
        if (args) {
            Value *other = eval_expr(it, args->item, env);
            if (other->kind == V_LIST)
                for (ListNode *c = other->as.list; c; c = c->next) value_list_append(list, c->item);
        }
        return value_void();
    }
    rt_error(it, line, "unknown list method '.%s'", name);
    return value_void();
}

/* Safe accessor for the common "this argument should be a string" case:
   returns the string, or "" if the value isn't actually a V_STRING (either
   because the wrong type was passed, or because evaluating it already
   failed upstream and short-circuited to void) - guards call sites below
   from reading the wrong union member (e.g. treating a `num`'s bit pattern
   as a `char*`, which is undefined behavior, not just a wrong answer). */
static const char *safe_str(Value *v) {
    return (v && v->kind == V_STRING) ? v->as.string : "";
}

typedef struct { Interp *it; Value *func; } TimerCallback;static void timer_trampoline(void *arg) {
    TimerCallback *tc = (TimerCallback *)arg;
    interp_lock();
    interp_invoke_closure(tc->it, tc->func, NULL, 0);
    interp_unlock();
}

static Value *builtin_dispatch(Interp *it, const char *ns, const char *method, ExprList *args, Env *env, int line, int *handled) {
    *handled = 1;
    if (strcmp(ns, "live") == 0) {
        if (strcmp(method, "on") == 0) {
            Value *v = args ? eval_expr(it, args->item, env) : value_void();
            if (it->had_runtime_error) return value_void();
            char *s = value_to_display_string(v);
            printf("%s\n", s);
            free(s);
            return value_void();
        }
        if (strcmp(method, "in") == 0) {
            if (args) {
                Value *prompt = eval_expr(it, args->item, env);
                char *s = value_to_display_string(prompt);
                printf("%s", s);
                fflush(stdout);
                free(s);
            }
            char buf[4096];
            if (!fgets(buf, sizeof(buf), stdin)) buf[0] = '\0';
            size_t l = strlen(buf);
            if (l > 0 && buf[l - 1] == '\n') buf[l - 1] = '\0';
            return value_string(buf);
        }
        if (strcmp(method, "deg") == 0) {
            /* live.deg(typeName, values...) - debug type assertion */
            if (!args) { rt_error(it, line, "live.deg expects at least a type name"); return value_void(); }
            Value *tname = eval_expr(it, args->item, env);
            const char *expect = safe_str(tname);
            ExprList *rest = args->next;
            int idx = 0;
            for (ExprList *c = rest; c; c = c->next, idx++) {
                Value *v = eval_expr(it, c->item, env);
                int ok = 0;
                if (strcmp(expect, "string") == 0) ok = v->kind == V_STRING;
                else if (strcmp(expect, "num") == 0) ok = v->kind == V_NUM;
                else if (strcmp(expect, "decimal") == 0) ok = v->kind == V_DECIMAL;
                else if (strcmp(expect, "bool") == 0) ok = v->kind == V_BOOL;
                else { printf("live.deg: not defined (unknown type '%s')\n", expect); continue; }
                if (!ok) printf("live.deg: type mismatch on argument %d (expected %s)\n", idx + 1, expect);
                else printf("live.deg: expectation met for argument %d\n", idx + 1);
            }
            return value_void();
        }
        if (strcmp(method, "after") == 0 || strcmp(method, "every") == 0) {
            if (!args || !args->next) { rt_error(it, line, "live.%s expects (delay_ms, callback)", method); return value_void(); }
            Value *delay_v = eval_expr(it, args->item, env);
            Value *fn = eval_expr(it, args->next->item, env);
            long delay = (long)value_as_number(delay_v);
            long interval = strcmp(method, "every") == 0 ? delay : 0;
            TimerCallback *tc = calloc(1, sizeof(TimerCallback));
            tc->it = it;
            tc->func = fn;
            int id = reactor_add_timer(delay, interval, timer_trampoline, tc);
            return value_num(id);
        }
        if (strcmp(method, "clear") == 0) {
            if (args) reactor_clear_timer((int)value_as_number(eval_expr(it, args->item, env)));
            return value_void();
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "json") == 0) {
        if (strcmp(method, "parse") == 0) {
            Value *v = args ? eval_expr(it, args->item, env) : value_string("");
            if (v->kind != V_STRING) { rt_error(it, line, "json.parse expects a string"); return value_void(); }
            return json_parse(v->as.string);
        }
        if (strcmp(method, "stringify") == 0) {
            Value *v = args ? eval_expr(it, args->item, env) : value_void();
            char *s = json_stringify(v);
            Value *out = value_string(s);
            free(s);
            return out;
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "env") == 0) {
        if (strcmp(method, "get") == 0) {
            if (!args) return value_void();
            Value *key = eval_expr(it, args->item, env);
            const char *val = getenv(safe_str(key));
            if (val) return value_string(val);
            if (args->next) return eval_expr(it, args->next->item, env);
            return value_void();
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "file") == 0) {
        if (strcmp(method, "on") == 0) {
            if (it->open_file) fclose((FILE*)it->open_file);
            Value *path = args ? eval_expr(it, args->item, env) : value_string("");
            it->open_file = fopen(safe_str(path), "a+");
            if (!it->open_file) rt_error(it, line, "cannot open file '%s'", safe_str(path));
            else rewind((FILE*)it->open_file);
            return value_void();
        }
        if (strcmp(method, "read") == 0) {
            if (!it->open_file) { rt_error(it, line, "file.read() with no file open (call file.on() first)"); return value_string(""); }
            fseek((FILE*)it->open_file, 0, SEEK_SET);
            size_t cap = 4096, len = 0;
            char *buf = malloc(cap);
            size_t n;
            while ((n = fread(buf + len, 1, cap - len, (FILE*)it->open_file)) > 0) {
                len += n;
                if (len == cap) { cap *= 2; buf = realloc(buf, cap); }
            }
            buf[len] = '\0';
            Value *v = value_string(buf);
            free(buf);
            return v;
        }
        if (strcmp(method, "loop") == 0) {
            /* line-by-line iteration is exposed via file.read() + string ops in this build */
            return value_void();
        }
        if (strcmp(method, "close") == 0) {
            if (it->open_file) { fclose((FILE*)it->open_file); it->open_file = NULL; }
            return value_void();
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "dir") == 0) {
        if (strcmp(method, "on") == 0) {
            Value *path = args ? eval_expr(it, args->item, env) : value_string(".");
            if (it->open_dir_path) free(it->open_dir_path);
            it->open_dir_path = strdup(safe_str(path));
            return value_void();
        }
        if (strcmp(method, "read") == 0 || strcmp(method, "loop") == 0) {
            Value *list = value_list_empty();
            if (it->open_dir_path) {
                DIR *d = opendir(it->open_dir_path);
                if (d) {
                    struct dirent *ent;
                    while ((ent = readdir(d))) {
                        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
                        value_list_append(list, value_string(ent->d_name));
                    }
                    closedir(d);
                }
            }
            return list;
        }
        if (strcmp(method, "close") == 0) return value_void();
        if (strcmp(method, "del") == 0) {
            if (it->open_dir_path) rmdir(it->open_dir_path);
            return value_void();
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "Task") == 0) {
        if (strcmp(method, "all") == 0) {
            /* Function calls (including async fun) already run synchronously
               to completion in this build (see DESIGN_DECISIONS.md), so by
               the time this list literal is evaluated every element is
               already a resolved value - Task.all just wraps it. */
            Value *v = args ? eval_expr(it, args->item, env) : value_list_empty();
            return task_done(v);
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "http") == 0) {
        if (strcmp(method, "get") == 0) {
            Value *url = args ? eval_expr(it, args->item, env) : value_string("");
            return http_client_get(safe_str(url));
        }
        if (strcmp(method, "post") == 0) {
            Value *url = args ? eval_expr(it, args->item, env) : value_string("");
            Value *body = args && args->next ? eval_expr(it, args->next->item, env) : NULL;
            return http_client_post(safe_str(url), body);
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "server") == 0) {
        if (strcmp(method, "on") == 0) {
            Value *port = args ? eval_expr(it, args->item, env) : value_num(8080);
            http_server_on(it, (int)value_as_number(port));
            return value_void();
        }
        if (strcmp(method, "route") == 0) {
            if (args && args->next && args->next->next) {
                Value *m = eval_expr(it, args->item, env);
                Value *pattern = eval_expr(it, args->next->item, env);
                Value *handler = eval_expr(it, args->next->next->item, env);
                http_server_route(safe_str(m), safe_str(pattern), handler);
            }
            return value_void();
        }
        if (strcmp(method, "listen") == 0) { http_server_listen(); return value_void(); }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "socket") == 0) {
        if (strcmp(method, "on") == 0) {
            Value *port = args ? eval_expr(it, args->item, env) : value_num(9090);
            ws_server_on(it, (int)value_as_number(port));
            return value_void();
        }
        if (strcmp(method, "route") == 0) {
            if (args && args->next) {
                Value *path = eval_expr(it, args->item, env);
                Value *handler = eval_expr(it, args->next->item, env);
                ws_server_route(safe_str(path), handler);
            }
            return value_void();
        }
        if (strcmp(method, "listen") == 0) { ws_server_listen(); return value_void(); }
        if (strcmp(method, "connect") == 0) {
            rt_error(it, line,
                "socket.connect(): the WebSocket *client* is not implemented in this build "
                "(the server side - socket.on/route/listen - is real; see DESIGN_DECISIONS.md)");
            return value_void();
        }
        *handled = 0;
        return value_void();
    }
    if (strcmp(ns, "worker") == 0) {
        if (strcmp(method, "spawn") == 0) {
            Value *fn = args ? eval_expr(it, args->item, env) : NULL;
            WorkerHandle *h = worker_spawn_func(it, fn);
            WorkerNative *wn = calloc(1, sizeof(WorkerNative));
            wn->handle = h;
            return native_wrap(NATIVE_WORKER, wn);
        }
        if (strcmp(method, "run") == 0) {
            if (args && args->next) {
                Value *path = eval_expr(it, args->item, env);
                Value *cb = eval_expr(it, args->next->item, env);
                WorkerHandle *h = worker_spawn_file(safe_str(path));
                Value *result = worker_handle_join(h);
                Value *cb_args[1] = { result };
                interp_invoke_closure(it, cb, cb_args, 1);
            }
            return value_void();
        }
        if (strcmp(method, "pool") == 0) {
            Value *n = args ? eval_expr(it, args->item, env) : value_num(4);
            WorkerPool *pool = worker_pool_create(it, (int)value_as_number(n));
            WorkerPoolNative *wpn = calloc(1, sizeof(WorkerPoolNative));
            wpn->pool = pool;
            return native_wrap(NATIVE_WORKER_POOL, wpn);
        }
        *handled = 0;
        return value_void();
    }
    *handled = 0;
    return value_void();
}

/* ---------------- binary/unary ops ---------------- */
static int is_numericish(Value *v) { return v->kind == V_NUM || v->kind == V_DECIMAL || v->kind == V_SCIFI || v->kind == V_BOOL; }

static Value *eval_binary(Interp *it, TokenType op, Value *l, Value *r, int line) {
    if (op == TOK_PLUS && (l->kind == V_STRING || r->kind == V_STRING)) {
        char *ls = value_to_display_string(l);
        char *rs = value_to_display_string(r);
        char *out = malloc(strlen(ls) + strlen(rs) + 1);
        strcpy(out, ls); strcat(out, rs);
        Value *v = value_string(out);
        free(ls); free(rs); free(out);
        return v;
    }
    if (op == TOK_EQ) return value_bool(value_equal(l, r));
    if (op == TOK_NE) return value_bool(!value_equal(l, r));
    if (op == TOK_AND) return value_bool(value_truthy(l) && value_truthy(r));
    if (op == TOK_OR) return value_bool(value_truthy(l) || value_truthy(r));

    if (!is_numericish(l) || !is_numericish(r)) {
        rt_error(it, line, "operator requires numeric operands");
        return value_void();
    }
    double ld = value_as_number(l), rd = value_as_number(r);
    switch (op) {
        case TOK_GT: return value_bool(ld > rd);
        case TOK_LT: return value_bool(ld < rd);
        case TOK_GE: return value_bool(ld >= rd);
        case TOK_LE: return value_bool(ld <= rd);
        case TOK_SHL: return value_num((long long)ld << (long long)rd);
        case TOK_SHR: return value_num((long long)ld >> (long long)rd);
        default: break;
    }
    int both_int = (l->kind == V_NUM && r->kind == V_NUM);
    switch (op) {
        case TOK_PLUS: return both_int ? value_num((long long)ld + (long long)rd) : value_decimal(ld + rd);
        case TOK_MINUS: return both_int ? value_num((long long)ld - (long long)rd) : value_decimal(ld - rd);
        case TOK_STAR: return both_int ? value_num((long long)ld * (long long)rd) : value_decimal(ld * rd);
        case TOK_SLASH:
            if (rd == 0) { rt_error(it, line, "division by zero"); return value_void(); }
            return value_decimal(ld / rd);
        case TOK_PERCENT:
            if ((long long)rd == 0) { rt_error(it, line, "modulo by zero"); return value_void(); }
            return both_int ? value_num((long long)ld % (long long)rd) : value_decimal(fmod(ld, rd));
        case TOK_POW: return both_int && rd >= 0 ? value_num((long long)pow(ld, rd)) : value_decimal(pow(ld, rd));
        default:
            rt_error(it, line, "unsupported binary operator");
            return value_void();
    }
}

static Value *get_lvalue_current(Interp *it, Expr *target, Env *env);
static void set_lvalue(Interp *it, Expr *target, Value *v, Env *env, int line);

static Value *eval_expr(Interp *it, Expr *e, Env *env) {
    if (it->had_runtime_error) return value_void();
    switch (e->kind) {
        case E_LIT_NUM: return value_num(e->as.num_lit);
        case E_LIT_DECIMAL: return value_decimal(e->as.decimal_lit);
        case E_LIT_SCIFI: return value_scifi(e->as.scifi_lit);
        case E_LIT_BOOL: return value_bool(e->as.bool_lit);
        case E_LIT_STRING_INTERP: return eval_string_interp(it, e, env);
        case E_IDENT: {
            Value *v = env_get(env, e->as.ident);
            if (!v) { rt_error(it, e->line, "undefined variable '%s'", e->as.ident); return value_void(); }
            return v;
        }
        case E_BINARY: {
            Value *l = eval_expr(it, e->as.binary.left, env);
            Value *r = eval_expr(it, e->as.binary.right, env);
            if (it->had_runtime_error) return value_void();
            return eval_binary(it, (TokenType)e->as.binary.op, l, r, e->line);
        }
        case E_UNARY: {
            Value *v = eval_expr(it, e->as.unary.operand, env);
            if (e->as.unary.op == TOK_NOT) return value_bool(!value_truthy(v));
            if (e->as.unary.op == TOK_MINUS) {
                if (v->kind == V_NUM) return value_num(-v->as.num);
                return value_decimal(-value_as_number(v));
            }
            return value_void();
        }
        case E_BETWEEN: {
            Value *v = eval_expr(it, e->as.between.value, env);
            Value *lo = eval_expr(it, e->as.between.low, env);
            Value *hi = eval_expr(it, e->as.between.high, env);
            double vd = value_as_number(v), lod = value_as_number(lo), hid = value_as_number(hi);
            return value_bool(vd >= lod && vd <= hid);
        }
        case E_TERNARY: {
            Value *c = eval_expr(it, e->as.ternary.cond, env);
            return value_truthy(c) ? eval_expr(it, e->as.ternary.then_e, env) : eval_expr(it, e->as.ternary.else_e, env);
        }
        case E_LIST: {
            Value *list = value_list_empty();
            for (ExprList *c = e->as.list_items; c; c = c->next) value_list_append(list, eval_expr(it, c->item, env));
            return list;
        }
        case E_DATA: {
            /* value_data_set prepends; display/stringify re-reverse to restore
               source order, so entries are inserted in plain encounter order here. */
            Value *data = value_data_empty();
            for (DataEntry *c = e->as.data_entries; c; c = c->next)
                value_data_set(data, c->key, eval_expr(it, c->value, env));
            return data;
        }
        case E_FUNC_LIT: {
            Value *v = value_new(V_FUNC);
            FuncValue *fv = calloc(1, sizeof(FuncValue));
            fv->params = e->as.func_lit.params;
            fv->body = e->as.func_lit.body;
            fv->closure = env;
            fv->is_async = e->as.func_lit.is_async;
            fv->name = strdup("<anonymous>");
            v->as.func = fv;
            return v;
        }
        case E_AWAIT: {
            Value *v = eval_expr(it, e->as.await_expr, env);
            if (it->had_runtime_error) return value_void();
            if (v->kind == V_NATIVE && ((NativeHeader *)v->as.native)->tag == NATIVE_TASK) {
                TaskNative *t = (TaskNative *)v->as.native;
                if (t->state == TASK_ERROR) { rt_error(it, e->line, "%s", t->error); return value_void(); }
                return t->result ? t->result : value_void();
            }
            /* Not a Task: function calls (including async fun) already run
               synchronously to completion in this build, so awaiting a
               plain value is a structural no-op - see DESIGN_DECISIONS.md. */
            return v;
        }
        case E_MEMBER: {
            Value *obj = eval_expr(it, e->as.member.object, env);
            if (it->had_runtime_error) return value_void();
            if (obj->kind == V_DATA) {
                Value *v = value_data_get(obj, e->as.member.name);
                return v ? v : value_void();
            }
            if (obj->kind == V_INSTANCE) {
                Instance *inst = (Instance *)obj->as.native;
                Value *v = value_data_get(inst->fields, e->as.member.name);
                if (v) return v;
                rt_error(it, e->line, "'%s' has no field '%s'", inst->class_info->name, e->as.member.name);
                return value_void();
            }
            rt_error(it, e->line, "cannot access member '%s' on this value", e->as.member.name);
            return value_void();
        }
        case E_INDEX: {
            Value *obj = eval_expr(it, e->as.index_expr.object, env);
            Value *idx = eval_expr(it, e->as.index_expr.index, env);
            if (obj->kind == V_LIST) {
                long long i = (long long)value_as_number(idx);
                long long k = 0;
                for (ListNode *c = obj->as.list; c; c = c->next, k++) if (k == i) return c->item;
                rt_error(it, e->line, "list index out of range");
                return value_void();
            }
            if (obj->kind == V_DATA) {
                Value *v = value_data_get(obj, safe_str(idx));
                return v ? v : value_void();
            }
            if (obj->kind == V_STRING) {
                long long i = (long long)value_as_number(idx);
                long long len = (long long)strlen(obj->as.string);
                if (i < 0 || i >= len) { rt_error(it, e->line, "string index out of range"); return value_void(); }
                char buf[2] = { obj->as.string[i], '\0' };
                return value_string(buf);
            }
            rt_error(it, e->line, "value is not indexable");
            return value_void();
        }
        case E_ASSIGN: {
            Value *newval;
            if (e->as.assign.op == TOK_ASSIGN) {
                newval = eval_expr(it, e->as.assign.value, env);
            } else {
                Value *cur = get_lvalue_current(it, e->as.assign.target, env);
                Value *rhs = eval_expr(it, e->as.assign.value, env);
                TokenType binop;
                switch (e->as.assign.op) {
                    case TOK_PLUS_EQ: binop = TOK_PLUS; break;
                    case TOK_MINUS_EQ: binop = TOK_MINUS; break;
                    case TOK_STAR_EQ: binop = TOK_STAR; break;
                    case TOK_SLASH_EQ: binop = TOK_SLASH; break;
                    case TOK_PERCENT_EQ: binop = TOK_PERCENT; break;
                    default: binop = TOK_POW; break;
                }
                newval = eval_binary(it, binop, cur, rhs, e->line);
            }
            set_lvalue(it, e->as.assign.target, newval, env, e->line);
            return newval;
        }
        case E_CALL: {
            Expr *callee = e->as.call.callee;
            if (callee->kind == E_MEMBER) {
                Expr *objExpr = callee->as.member.object;
                const char *name = callee->as.member.name;
                if (objExpr->kind == E_IDENT && !env_get(env, objExpr->as.ident)) {
                    static const char *builtin_ns[] = {"live","json","env","file","dir","Task","server","socket","worker","http",NULL};
                    for (int i = 0; builtin_ns[i]; i++) {
                        if (strcmp(objExpr->as.ident, builtin_ns[i]) == 0) {
                            int handled = 0;
                            Value *r = builtin_dispatch(it, objExpr->as.ident, name, e->as.call.args, env, e->line, &handled);
                            if (handled) return r;
                            break;
                        }
                    }
                }
                Value *obj = eval_expr(it, objExpr, env);
                if (it->had_runtime_error) return value_void();
                if (obj->kind == V_LIST) return call_list_method(it, obj, name, e->as.call.args, env, e->line);
                if (obj->kind == V_INSTANCE) {
                    Instance *inst = (Instance *)obj->as.native;
                    ClassInfo *defining;
                    Stmt *method_stmt;
                    if (find_method(inst->class_info, name, &defining, &method_stmt)) {
                        int argc;
                        Value **argv2 = eval_args(it, e->as.call.args, env, &argc);
                        Value *r = invoke_method(it, defining, method_stmt, obj, argv2, argc);
                        free(argv2);
                        return r;
                    }
                    rt_error(it, e->line, "'%s' has no method '%s'", inst->class_info->name, name);
                    return value_void();
                }
                if (http_is_response_object(obj)) {
                    int argc = 0;
                    for (ExprList *c = e->as.call.args; c; c = c->next) argc++;
                    Value **argv = argc ? malloc(sizeof(Value *) * argc) : NULL;
                    int i = 0;
                    for (ExprList *c = e->as.call.args; c; c = c->next) argv[i++] = eval_expr(it, c->item, env);
                    Value *r = http_response_call_method(obj, name, argv, argc);
                    free(argv);
                    if (r) return r;
                    rt_error(it, e->line, "unknown response method '.%s'", name);
                    return value_void();
                }
                if (obj->kind == V_NATIVE) {
                    NativeTag tag = ((NativeHeader *)obj->as.native)->tag;
                    int argc = 0;
                    for (ExprList *c = e->as.call.args; c; c = c->next) argc++;
                    Value **argv = argc ? malloc(sizeof(Value *) * argc) : NULL;
                    int i = 0;
                    for (ExprList *c = e->as.call.args; c; c = c->next) argv[i++] = eval_expr(it, c->item, env);

                    if (tag == NATIVE_TASK) {
                        TaskNative *t = (TaskNative *)obj->as.native;
                        Value *result = value_void();
                        if (strcmp(name, "then") == 0) {
                            if (t->state == TASK_DONE && argc >= 1) { Value *a[1] = { t->result }; interp_invoke_closure(it, argv[0], a, 1); }
                            result = obj;
                        } else if (strcmp(name, "catch") == 0) {
                            if (t->state == TASK_ERROR && argc >= 1) { Value *a[1] = { value_string(t->error) }; interp_invoke_closure(it, argv[0], a, 1); }
                            result = obj;
                        } else if (strcmp(name, "await") == 0) {
                            if (t->state == TASK_ERROR) rt_error(it, e->line, "%s", t->error);
                            else result = t->result ? t->result : value_void();
                        } else {
                            rt_error(it, e->line, "unknown Task method '.%s'", name);
                        }
                        free(argv);
                        return result;
                    }
                    if (tag == NATIVE_WORKER) {
                        WorkerNative *wn = (WorkerNative *)obj->as.native;
                        Value *result = value_void();
                        if (strcmp(name, "on") == 0 && argc >= 2 && argv[0]->kind == V_STRING && strcmp(argv[0]->as.string, "done") == 0) {
                            Value *wr = worker_handle_join(wn->handle);
                            Value *a[1] = { wr };
                            interp_invoke_closure(it, argv[1], a, 1);
                        } else if (strcmp(name, "terminate") == 0) {
                            worker_handle_join(wn->handle); /* best-effort: see DESIGN_DECISIONS.md */
                        } else {
                            rt_error(it, e->line, "unknown Worker method '.%s'", name);
                        }
                        free(argv);
                        return result;
                    }
                    if (tag == NATIVE_WORKER_POOL) {
                        WorkerPoolNative *wpn = (WorkerPoolNative *)obj->as.native;
                        Value *result = value_void();
                        if (strcmp(name, "submit") == 0 && argc >= 1) {
                            worker_pool_submit(wpn->pool, argv[0]);
                        } else if (strcmp(name, "onAll") == 0 && argc >= 1) {
                            Value *results = worker_pool_wait_all(wpn->pool);
                            Value *a[1] = { results };
                            interp_invoke_closure(it, argv[0], a, 1);
                        } else {
                            rt_error(it, e->line, "unknown Worker pool method '.%s'", name);
                        }
                        free(argv);
                        return result;
                    }
                    if (tag == NATIVE_SOCKET_CONN) {
                        Value *r = ws_conn_call_method(obj, name, argv, argc);
                        free(argv);
                        if (r) return r;
                        rt_error(it, e->line, "unknown Socket method '.%s'", name);
                        return value_void();
                    }
                    if (tag == NATIVE_SUPER_REF) {
                        SuperRefNative *sr = (SuperRefNative *)obj->as.native;
                        ClassInfo *defining;
                        Stmt *method_stmt;
                        if (sr->parent_class && find_method(sr->parent_class, name, &defining, &method_stmt)) {
                            Value *r = invoke_method(it, defining, method_stmt, sr->this_instance, argv, argc);
                            free(argv);
                            return r;
                        }
                        free(argv);
                        rt_error(it, e->line, "no parent method '%s' to call via super", name);
                        return value_void();
                    }
                    free(argv);
                }
                rt_error(it, e->line, "unknown method '.%s' for this value", name);
                return value_void();
            }
            Value *fn = eval_expr(it, callee, env);
            if (it->had_runtime_error) return value_void();
            if (fn->kind == V_NATIVE && ((NativeHeader *)fn->as.native)->tag == NATIVE_SUPER_REF) {
                /* bare `super(args)`: call the parent class's constructor,
                   bound to the same instance the current method/constructor
                   is running on. */
                SuperRefNative *sr = (SuperRefNative *)fn->as.native;
                ClassInfo *ctor_class;
                Stmt *ctor_stmt;
                if (sr->parent_class && find_method(sr->parent_class, "constructor", &ctor_class, &ctor_stmt)) {
                    int argc;
                    Value **argv = eval_args(it, e->as.call.args, env, &argc);
                    Value *r = invoke_method(it, ctor_class, ctor_stmt, sr->this_instance, argv, argc);
                    free(argv);
                    return r;
                }
                return value_void(); /* no parent constructor to call - a no-op, not an error */
            }
            int threw = 0;
            Value *r = call_value(it, fn, e->as.call.args, env, &threw, e->line);
            return r;
        }
        case E_NEW: {
            Value *cls_val = env_get(env, e->as.new_expr.class_name);
            if (!cls_val || cls_val->kind != V_CLASS) {
                rt_error(it, e->line, "'%s' is not a known class", e->as.new_expr.class_name);
                return value_void();
            }
            return construct_instance(it, (ClassInfo *)cls_val->as.native, e->as.new_expr.args, env, e->line);
        }
    }
    return value_void();
}

static Value *get_lvalue_current(Interp *it, Expr *target, Env *env) {
    return eval_expr(it, target, env);
}

static int is_reserved_namespace(const char *name) {
    static const char *ns[] = {"live","json","env","file","dir","Task","server","socket","worker","http",NULL};
    for (int i = 0; ns[i]; i++) if (strcmp(name, ns[i]) == 0) return 1;
    return 0;
}

static void set_lvalue(Interp *it, Expr *target, Value *v, Env *env, int line) {
    if (target->kind == E_IDENT) {
        if (is_reserved_namespace(target->as.ident) && !env_get(env, target->as.ident)) {
            /* e.g. the file-level `live = "1";` live-mode directive: recognized,
               but doesn't shadow the reserved 'live' builtin namespace. */
            return;
        }
        if (env_is_fixed(env, target->as.ident)) {
            rt_error(it, line, "cannot reassign fixed variable '%s'", target->as.ident);
            return;
        }
        if (!env_set(env, target->as.ident, v)) env_define(env, target->as.ident, v, 0);
        return;
    }
    if (target->kind == E_INDEX) {
        Value *obj = eval_expr(it, target->as.index_expr.object, env);
        Value *idx = eval_expr(it, target->as.index_expr.index, env);
        if (obj->kind == V_LIST) {
            long long i = (long long)value_as_number(idx);
            long long k = 0;
            for (ListNode *c = obj->as.list; c; c = c->next, k++) if (k == i) { c->item = v; return; }
            rt_error(it, line, "list index out of range");
            return;
        }
        if (obj->kind == V_DATA) { value_data_set(obj, safe_str(idx), v); return; }
        rt_error(it, line, "value is not assignable by index");
        return;
    }
    if (target->kind == E_MEMBER) {
        Value *obj = eval_expr(it, target->as.member.object, env);
        if (obj->kind == V_DATA) { value_data_set(obj, target->as.member.name, v); return; }
        if (obj->kind == V_INSTANCE) {
            Instance *inst = (Instance *)obj->as.native;
            value_data_set(inst->fields, target->as.member.name, v);
            return;
        }
        rt_error(it, line, "cannot assign member '%s' on this value", target->as.member.name);
        return;
    }
    rt_error(it, line, "invalid assignment target");
}

static Value *call_value(Interp *it, Value *callee_fn, ExprList *args, Env *env, int *threw, int line) {
    *threw = 0;
    if (!callee_fn || callee_fn->kind != V_FUNC) {
        rt_error(it, line, "attempted to call a non-function value");
        return value_void();
    }
    FuncValue *fn = callee_fn->as.func;
    Env *call_env = env_new(fn->closure);
    Param *p = fn->params;
    ExprList *a = args;
    while (p) {
        Value *argv = a ? eval_expr(it, a->item, env) : value_void();
        env_define(call_env, p->name, argv, 0);
        p = p->next;
        if (a) a = a->next;
    }
    ExecResult r = exec_block(it, fn->body->as.block, call_env);
    if (r.sig == SIG_RETURN) return r.val ? r.val : value_void();
    if (r.sig == SIG_THROW) {
        /* uncaught throw escaping a function call: surface as runtime error */
        if (!it->had_runtime_error) {
            char *s = value_to_display_string(r.val);
            rt_error(it, line, "uncaught error: %s", s);
            free(s);
        }
        *threw = 1;
    }
    return value_void();
}

/* ---------------- public entry points for the HTTP server / worker
   subsystem, which call into Jaguar functions without an existing
   Expr/Env evaluation context (see interp.h) ---------------- */
#include <pthread.h>
static pthread_mutex_t g_interp_lock = PTHREAD_MUTEX_INITIALIZER;
void interp_lock(void) { pthread_mutex_lock(&g_interp_lock); }
void interp_unlock(void) { pthread_mutex_unlock(&g_interp_lock); }

Value *interp_invoke(Interp *it, Value *callee_fn, Value **argv, int argc) {
    if (!callee_fn || callee_fn->kind != V_FUNC) {
        rt_error(it, 0, "attempted to call a non-function value");
        return value_void();
    }
    FuncValue *fn = callee_fn->as.func;
    /* Rooted at globals rather than fn->closure: this is what keeps worker
       calls from reaching into the main thread's live local closures (see
       DESIGN_DECISIONS.md - "worker isolation"). Route handlers passed to
       server.route()/socket.route() are ordinary function literals whose
       closure IS the top-level scope, so this is transparent for them. */
    Env *call_env = env_new(it->globals);
    Param *p = fn->params;
    int i = 0;
    while (p) {
        Value *v = (i < argc) ? argv[i] : value_void();
        env_define(call_env, p->name, v, 0);
        p = p->next;
        i++;
    }
    ExecResult r = exec_block(it, fn->body->as.block, call_env);
    if (r.sig == SIG_RETURN) return r.val ? r.val : value_void();
    return value_void();
}

Value *interp_invoke_closure(Interp *it, Value *callee_fn, Value **argv, int argc) {
    if (!callee_fn || callee_fn->kind != V_FUNC) {
        rt_error(it, 0, "attempted to call a non-function value");
        return value_void();
    }
    FuncValue *fn = callee_fn->as.func;
    Env *call_env = env_new(fn->closure); /* preserves the function's normal lexical scope */
    Param *p = fn->params;
    int i = 0;
    while (p) {
        Value *v = (i < argc) ? argv[i] : value_void();
        env_define(call_env, p->name, v, 0);
        p = p->next;
        i++;
    }
    ExecResult r = exec_block(it, fn->body->as.block, call_env);
    if (r.sig == SIG_RETURN) return r.val ? r.val : value_void();
    return value_void();
}

/* Hoist top-level / block-level function declarations so calls can appear
   before their textual definition, matching ordinary scripting-language
   expectations. */
static void hoist_functions(Interp *it, StmtList *list, Env *env) {
    (void)it;
    for (StmtList *c = list; c; c = c->next) {
        Stmt *s = c->item;
        if (s->kind == S_FUN_DECL) {
            Value *v = value_new(V_FUNC);
            FuncValue *fv = calloc(1, sizeof(FuncValue));
            fv->params = s->as.fun_decl.params;
            fv->body = s->as.fun_decl.body;
            fv->closure = env;
            fv->is_async = s->as.fun_decl.is_async;
            fv->name = strdup(s->as.fun_decl.name);
            v->as.func = fv;
            env_define(env, s->as.fun_decl.name, v, 1);
        }
    }
}

/* Two passes so classes can reference each other regardless of textual
   order (matching the existing function-hoisting behavior): first
   register every class as a ClassInfo, then resolve each `extends`
   parent_name to the actual ClassInfo pointer now that all names exist. */
static void hoist_classes(Interp *it, StmtList *list, Env *env) {
    (void)it;
    for (StmtList *c = list; c; c = c->next) {
        Stmt *s = c->item;
        if (s->kind == S_CLASS) {
            ClassInfo *ci = calloc(1, sizeof(ClassInfo));
            ci->name = strdup(s->as.class_decl.name);
            ci->parent_name = s->as.class_decl.parent_name ? strdup(s->as.class_decl.parent_name) : NULL;
            ci->members = s->as.class_decl.members;
            ci->closure = env;
            Value *v = value_new(V_CLASS);
            v->as.native = ci;
            env_define(env, ci->name, v, 1);
        }
    }
    for (StmtList *c = list; c; c = c->next) {
        Stmt *s = c->item;
        if (s->kind == S_CLASS && s->as.class_decl.parent_name) {
            Value *self_v = env_get(env, s->as.class_decl.name);
            Value *parent_v = env_get(env, s->as.class_decl.parent_name);
            if (self_v && self_v->kind == V_CLASS) {
                ClassInfo *ci = (ClassInfo *)self_v->as.native;
                if (parent_v && parent_v->kind == V_CLASS) ci->parent = (ClassInfo *)parent_v->as.native;
                /* an unresolvable parent_name is left NULL and surfaces as a
                   runtime error only if/when `new`/`super` actually needs it -
                   this mirrors how undefined identifiers are handled elsewhere */
            }
        }
    }
}

/* Walks a class and its ancestors for a method (S_FUN_DECL member) named
   `name`. On success, *out_defining is the class where it was actually
   found (which may be an ancestor of `start`) and *out_stmt is the method's
   S_FUN_DECL node. Returns 1 if found, 0 otherwise. */
static int find_method(ClassInfo *start, const char *name, ClassInfo **out_defining, Stmt **out_stmt) {
    for (ClassInfo *ci = start; ci; ci = ci->parent) {
        for (StmtList *c = ci->members; c; c = c->next) {
            if (c->item->kind == S_FUN_DECL && strcmp(c->item->as.fun_decl.name, name) == 0) {
                *out_defining = ci;
                *out_stmt = c->item;
                return 1;
            }
        }
    }
    return 0;
}

static void init_fields_recursive(Interp *it, ClassInfo *ci, Value *fields) {
    if (ci->parent) init_fields_recursive(it, ci->parent, fields);
    Env *field_env = env_new(ci->closure);
    for (StmtList *c = ci->members; c; c = c->next) {
        if (c->item->kind == S_VARDECL) {
            Value *v = c->item->as.vardecl.init ? eval_expr(it, c->item->as.vardecl.init, field_env) : value_void();
            value_data_set(fields, c->item->as.vardecl.name, v);
        }
    }
}

/* Evaluates an ExprList of call arguments into a malloc'd Value* array
   (NULL/argc==0 if there are none). Shared by every call site that needs
   pre-evaluated args (constructors, methods, super calls) so arguments
   with side effects are evaluated exactly once. */
static Value **eval_args(Interp *it, ExprList *args, Env *env, int *out_argc) {
    int argc = 0;
    for (ExprList *c = args; c; c = c->next) argc++;
    Value **argv = argc ? malloc(sizeof(Value *) * argc) : NULL;
    int i = 0;
    for (ExprList *c = args; c; c = c->next) argv[i++] = eval_expr(it, c->item, env);
    *out_argc = argc;
    return argv;
}

/* Calls a method/constructor S_FUN_DECL's body with `this` (and `super`,
   if `defining_class` has a parent) bound in a fresh call_env rooted at
   the class's closure - not the caller's env, so methods see their own
   class's lexical scope, not whatever scope happened to call them.
   `argv`/`argc` must already be evaluated (see eval_args) - this function
   never evaluates argument expressions itself, so callers control exactly
   once, in exactly the right env, each argument expression runs. */
static Value *invoke_method(Interp *it, ClassInfo *defining_class, Stmt *method_stmt, Value *this_instance, Value **argv, int argc) {
    Env *call_env = env_new(defining_class->closure);
    env_define(call_env, "this", this_instance, 1);
    if (defining_class->parent) {
        SuperRefNative *sr = calloc(1, sizeof(SuperRefNative));
        sr->parent_class = defining_class->parent;
        sr->this_instance = this_instance;
        Value *super_v = native_wrap(NATIVE_SUPER_REF, sr);
        env_define(call_env, "super", super_v, 1);
    }
    Param *p = method_stmt->as.fun_decl.params;
    int i = 0;
    while (p) {
        Value *v = (i < argc) ? argv[i] : value_void();
        env_define(call_env, p->name, v, 0);
        p = p->next;
        i++;
    }
    ExecResult r = exec_block(it, method_stmt->as.fun_decl.body->as.block, call_env);
    return r.sig == SIG_RETURN && r.val ? r.val : value_void();
}

static Value *construct_instance(Interp *it, ClassInfo *ci, ExprList *args, Env *env, int line) {
    Value *fields = value_data_empty();
    init_fields_recursive(it, ci, fields);
    Instance *inst = calloc(1, sizeof(Instance));
    inst->class_info = ci;
    inst->fields = fields;
    Value *instance_val = value_new(V_INSTANCE);
    instance_val->as.native = inst;

    ClassInfo *ctor_class;
    Stmt *ctor_stmt;
    if (find_method(ci, "constructor", &ctor_class, &ctor_stmt)) {
        int argc;
        Value **argv = eval_args(it, args, env, &argc);
        invoke_method(it, ctor_class, ctor_stmt, instance_val, argv, argc);
        free(argv);
    }
    /* no constructor anywhere in the chain: fields keep their declared
       defaults, matching a language with no explicit "default constructor"
       concept rather than erroring on a perfectly legal `new X()`. */
    (void)line;
    return instance_val;
}

static ExecResult exec_block(Interp *it, StmtList *list, Env *parent_env) {
    Env *env = env_new(parent_env);
    hoist_classes(it, list, env);
    hoist_functions(it, list, env);
    for (StmtList *c = list; c; c = c->next) {
        ExecResult r = exec_stmt(it, c->item, env);
        if (r.sig != SIG_NONE || it->had_runtime_error) return r;
    }
    return res_none();
}

static ExecResult exec_stmt(Interp *it, Stmt *s, Env *env) {
    if (it->had_runtime_error) return res_none();
    switch (s->kind) {
        case S_VARDECL: {
            Value *v = s->as.vardecl.init ? eval_expr(it, s->as.vardecl.init, env) : value_void();
            if (it->had_runtime_error) return res_none();
            env_define(env, s->as.vardecl.name, v, s->as.vardecl.is_fixed);
            return res_none();
        }
        case S_EXPR:
            eval_expr(it, s->as.expr_stmt, env);
            return res_none();
        case S_IF: {
            Value *c = eval_expr(it, s->as.if_stmt.cond, env);
            if (it->had_runtime_error) return res_none();
            if (value_truthy(c)) return exec_stmt(it, s->as.if_stmt.then_b, env);
            for (ElifChain *e = s->as.if_stmt.elifs; e; e = e->next) {
                Value *ec = eval_expr(it, e->cond, env);
                if (value_truthy(ec)) return exec_stmt(it, e->body, env);
            }
            if (s->as.if_stmt.else_b) return exec_stmt(it, s->as.if_stmt.else_b, env);
            return res_none();
        }
        case S_LOOP_WHILE: {
            while (value_truthy(eval_expr(it, s->as.loop_while.cond, env))) {
                if (it->had_runtime_error) break;
                ExecResult r = exec_stmt(it, s->as.loop_while.body, env);
                if (r.sig != SIG_NONE) return r;
                if (it->had_runtime_error) break;
            }
            return res_none();
        }
        case S_DO_WHILE: {
            do {
                ExecResult r = exec_stmt(it, s->as.do_while.body, env);
                if (r.sig != SIG_NONE) return r;
                if (it->had_runtime_error) break;
            } while (value_truthy(eval_expr(it, s->as.do_while.cond, env)) && !it->had_runtime_error);
            return res_none();
        }
        case S_FOR_IN: {
            Value *coll = eval_expr(it, s->as.for_in.iterable, env);
            if (coll->kind == V_LIST) {
                for (ListNode *c = coll->as.list; c; c = c->next) {
                    Env *loop_env = env_new(env);
                    env_define(loop_env, s->as.for_in.var_name, c->item, 0);
                    if (s->as.for_in.cond && !value_truthy(eval_expr(it, s->as.for_in.cond, loop_env))) continue;
                    ExecResult r = exec_stmt(it, s->as.for_in.body, loop_env);
                    if (r.sig != SIG_NONE) return r;
                    if (it->had_runtime_error) break;
                }
            } else {
                rt_error(it, s->line, "for-in requires a list");
            }
            return res_none();
        }
        case S_ITERATE: {
            Value *coll = eval_expr(it, s->as.iterate.collection, env);
            if (coll->kind == V_LIST) {
                for (ListNode *c = coll->as.list; c; c = c->next) {
                    Env *loop_env = env_new(env);
                    env_define(loop_env, s->as.iterate.item_name, c->item, 0);
                    ExecResult r = exec_stmt(it, s->as.iterate.body, loop_env);
                    if (r.sig != SIG_NONE) return r;
                    if (it->had_runtime_error) break;
                }
            } else if (coll->kind == V_DATA) {
                DataNode *rev = NULL;
                for (DataNode *c = coll->as.data; c; c = c->next) {
                    DataNode *n = malloc(sizeof(DataNode)); n->key = c->key; n->val = c->val; n->next = rev; rev = n;
                }
                for (DataNode *c = rev; c; c = c->next) {
                    Env *loop_env = env_new(env);
                    env_define(loop_env, s->as.iterate.item_name, c->val, 0);
                    ExecResult r = exec_stmt(it, s->as.iterate.body, loop_env);
                    if (r.sig != SIG_NONE) return r;
                    if (it->had_runtime_error) break;
                }
            } else {
                rt_error(it, s->line, "iterate() requires a list or data collection");
            }
            return res_none();
        }
        case S_FUN_DECL:
            return res_none(); /* already hoisted */
        case S_RETURN: {
            Value *v = s->as.return_expr ? eval_expr(it, s->as.return_expr, env) : value_void();
            return res_return(v);
        }
        case S_BLOCK:
            return exec_block(it, s->as.block, env);
        case S_CLASS: case S_ENUM: case S_STRUCT: case S_IMPORT: case S_EXPORT:
            return res_none(); /* declarations only; see DESIGN_DECISIONS.md */
        case S_TRY_CATCH: {
            int saved_error_flag = it->had_runtime_error;
            char *saved_msg = it->error_message;
            it->error_message = NULL;
            it->had_runtime_error = 0;
            ExecResult r = exec_stmt(it, s->as.try_catch.try_b, env);
            if (r.sig == SIG_THROW || it->had_runtime_error) {
                Env *catch_env = env_new(env);
                const char *msg = it->error_message ? it->error_message : "error";
                env_define(catch_env, s->as.try_catch.err_name, value_string(msg), 0);
                if (it->error_message) { free(it->error_message); it->error_message = NULL; }
                it->had_runtime_error = 0;
                free(saved_msg);
                return exec_stmt(it, s->as.try_catch.catch_b, catch_env);
            }
            it->had_runtime_error = saved_error_flag;
            it->error_message = saved_msg;
            return res_none();
        }
        case S_BREAK: case S_CONTINUE:
            return res_none();
    }
    return res_none();
}

int interp_run(Interp *it, StmtList *program) {
    /* Executed directly against it->globals (not a child scope, unlike
       ordinary blocks) so that top-level functions/fixed constants are
       genuinely IN globals - that's what worker threads (interp_invoke)
       and HTTP/WS route handlers (interp_invoke_closure, whose closure
       chain bottoms out here) actually see. */
    hoist_classes(it, program, it->globals);
    hoist_functions(it, program, it->globals);
    for (StmtList *c = program; c; c = c->next) {
        ExecResult r = exec_stmt(it, c->item, it->globals);
        if (r.sig != SIG_NONE || it->had_runtime_error) break;
    }
    if (it->open_file) { fclose((FILE*)it->open_file); it->open_file = NULL; }
    if (it->had_runtime_error) {
        fprintf(stderr, "runtime error: %s\n", it->error_message ? it->error_message : "unknown error");
        return 1;
    }
    return 0;
}
