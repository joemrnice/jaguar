#define _POSIX_C_SOURCE 200809L
#include "typecheck.h"
#include "token.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* ---------------------------------------------------------------------
   A best-effort static checker: full Hindley-Milner-style inference is
   out of scope (see DESIGN_DECISIONS.md), but this does real, useful
   work beyond syntax:
     - every var/fixed still needs a type annotation (unchanged)
     - fixed reassignment is still rejected (unchanged)
     - function/method/constructor CALLS are checked against the
       callee's declared parameter types, when both the callee and the
       argument's type are known
     - RETURN statements are checked against the enclosing function's
       declared return type
     - `new X(...)` is rejected at typecheck time if X isn't a known class

   `infer_type` is a PURE function - it never emits diagnostics and
   never re-runs checks, only computes a best-effort type (or NULL for
   "unknown"). All error emission happens in check_expr/check_stmt,
   each exactly once per node, specifically so that asking for a type
   never has the side effect of re-reporting an error someone else
   already reported (a real bug caught in testing - see
   DESIGN_DECISIONS.md's typechecker section).

   "Known type" is deliberately conservative: literals, declared
   variables/parameters/fields, and calls whose return type is declared.
   Anything else (this, builtin return values, generic list/data
   contents) is unknown and never flagged - only a confident mismatch
   between two known concrete types is an error. num/decimal/scifi
   interoperate freely (matching the interpreter's own arithmetic
   promotion), and so do list/MixedList (a `[...]` literal can't know
   which one it's meant to be - that's purely up to the declared type).
   --------------------------------------------------------------------- */

typedef struct Sym {
    char *name;
    int is_fixed;
    TypeInfo *type;       /* NULL = unknown */
    Param *fn_params;      /* non-NULL if this symbol is a function */
    TypeInfo *fn_return;   /* return type, if fn_params is set */
    struct Sym *next;
} Sym;

typedef struct Scope { Sym *syms; struct Scope *parent; } Scope;

/* A class's flattened signature: name, resolved parent (for inherited
   lookups), and its own members (fields + methods) as parsed. */
typedef struct ClassSig {
    char *name;
    struct ClassSig *parent;
    StmtList *members;
    struct ClassSig *next;
} ClassSig;

typedef struct {
    const char *filename;
    int errors;
    int in_async;           /* depth of async fun nesting */
    TypeInfo *cur_return;    /* enclosing function's declared return type, or NULL */
    ClassSig *classes;        /* all classes in the program, name-addressable */
} TC;

static Scope *scope_push(Scope *parent) {
    Scope *s = calloc(1, sizeof(Scope));
    s->parent = parent;
    return s;
}

static Sym *scope_define(Scope *s, const char *name, int is_fixed, TypeInfo *type) {
    Sym *sym = calloc(1, sizeof(Sym));
    sym->name = strdup(name);
    sym->is_fixed = is_fixed;
    sym->type = type;
    sym->next = s->syms;
    s->syms = sym;
    return sym;
}

static Sym *scope_find(Scope *s, const char *name) {
    for (Scope *c = s; c; c = c->parent)
        for (Sym *sym = c->syms; sym; sym = sym->next)
            if (strcmp(sym->name, name) == 0) return sym;
    return NULL;
}

static void tc_error(TC *tc, int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: type error: ", tc->filename ? tc->filename : "<input>", line);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n");
    tc->errors++;
}

static ClassSig *find_class(TC *tc, const char *name) {
    for (ClassSig *c = tc->classes; c; c = c->next) if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

/* Walks a class and its ancestors for a member (field or method,
   depending on `want_method`) named `name`. */
static Stmt *find_class_member(ClassSig *start, const char *name, int want_method) {
    for (ClassSig *c = start; c; c = c->parent) {
        for (StmtList *m = c->members; m; m = m->next) {
            Stmt *s = m->item;
            if (want_method && s->kind == S_FUN_DECL && strcmp(s->as.fun_decl.name, name) == 0) return s;
            if (!want_method && s->kind == S_VARDECL && strcmp(s->as.vardecl.name, name) == 0) return s;
        }
    }
    return NULL;
}

/* num/decimal/scifi freely interoperate (matches the interpreter's own
   arithmetic promotion rules); list/MixedList/vector/matrix all do too,
   since a `[...]` (or `[[...]]`) literal can't know which of those it's
   meant to be - and vector/matrix are backed by ordinary lists at
   runtime anyway (see DESIGN_DECISIONS.md) - one "family" each. */
static JagType type_family(JagType t) {
    if (t == T_NUM || t == T_DECIMAL || t == T_SCIFI) return T_NUM;
    if (t == T_MIXEDLIST || t == T_VECTOR || t == T_MATRIX) return T_LIST;
    return t;
}

/* Only flags a *confident* mismatch: both sides must be concrete,
   known types. Unknown/void on either side is never an error here. */
static int types_conflict(TypeInfo *declared, TypeInfo *actual) {
    if (!declared || !actual) return 0;
    if (declared->kind == T_UNKNOWN || actual->kind == T_UNKNOWN) return 0;
    if (declared->kind == T_VOID || actual->kind == T_VOID) return 0;
    /* a named class type only conflicts with another *different* named
       class type - never against anything we can't pin down precisely */
    if (declared->kind == T_CLASS_NAMED && actual->kind == T_CLASS_NAMED)
        return strcmp(declared->name, actual->name) != 0;
    if (declared->kind == T_CLASS_NAMED || actual->kind == T_CLASS_NAMED) return 0;
    return type_family(declared->kind) != type_family(actual->kind);
}

static const char *type_name(TypeInfo *t) {
    if (!t) return "unknown";
    switch (t->kind) {
        case T_STRING: return "string"; case T_NUM: return "num";
        case T_DECIMAL: return "decimal"; case T_BOOL: return "bool";
        case T_SCIFI: return "scifi"; case T_DATA: return "data";
        case T_LIST: return "list"; case T_MIXEDLIST: return "MixedList";
        case T_ENUM: return "enum"; case T_STRUCT: return "struct";
        case T_VECTOR: return "vector"; case T_MATRIX: return "matrix";
        case T_TASK: return "Task"; case T_WORKER: return "Worker";
        case T_SOCKET: return "Socket"; case T_VOID: return "void";
        case T_CLASS_NAMED: return t->name;
        default: return "unknown";
    }
}

static void check_expr(TC *tc, Expr *e, Scope *scope);
static void check_stmt(TC *tc, Stmt *s, Scope *scope);
static void check_block(TC *tc, StmtList *list, Scope *parent);
static TypeInfo *infer_type(TC *tc, Expr *e, Scope *scope);

static void check_expr_list(TC *tc, ExprList *l, Scope *scope) {
    for (; l; l = l->next) check_expr(tc, l->item, scope);
}

/* Checks already-parsed call arguments against a known parameter list, by
   position (extra args on either side are simply not compared - default
   values / varargs aren't modeled). Pure with respect to `args` itself:
   the caller is responsible for also recursing into check_expr on each
   argument for deeper checks; this only compares inferred types. */
static void check_call_args(TC *tc, const char *callee_label, Param *params, ExprList *args, Scope *scope, int line) {
    Param *p = params;
    ExprList *a = args;
    int idx = 1;
    while (p && a) {
        TypeInfo *actual = infer_type(tc, a->item, scope);
        if (types_conflict(p->type, actual))
            tc_error(tc, line, "%s: argument %d expected %s, got %s", callee_label, idx, type_name(p->type), type_name(actual));
        p = p->next;
        a = a->next;
        idx++;
    }
}

/* Pure: computes a best-effort type for `e`, or NULL if unknown. Never
   calls tc_error and never re-checks call arguments (check_expr already
   did, or will) - see the file header for why that separation matters. */
static TypeInfo *infer_type(TC *tc, Expr *e, Scope *scope) {
    if (!e) return NULL;
    switch (e->kind) {
        case E_LIT_NUM: return type_new(T_NUM);
        case E_LIT_DECIMAL: return type_new(T_DECIMAL);
        case E_LIT_SCIFI: return type_new(T_SCIFI);
        case E_LIT_BOOL: return type_new(T_BOOL);
        case E_LIT_STRING_INTERP: return type_new(T_STRING);
        case E_LIST: return type_new(T_LIST);
        case E_DATA: return type_new(T_DATA);
        case E_IDENT: {
            Sym *sym = scope_find(scope, e->as.ident);
            return sym ? sym->type : NULL;
        }
        case E_TERNARY: {
            TypeInfo *a = infer_type(tc, e->as.ternary.then_e, scope);
            TypeInfo *b = infer_type(tc, e->as.ternary.else_e, scope);
            if (a && b && !types_conflict(a, b)) return a;
            return NULL;
        }
        case E_BETWEEN: return type_new(T_BOOL);
        case E_BINARY:
            switch (e->as.binary.op) {
                case TOK_GT: case TOK_LT: case TOK_GE: case TOK_LE:
                case TOK_EQ: case TOK_NE: case TOK_AND: case TOK_OR:
                    return type_new(T_BOOL);
                case TOK_PLUS: {
                    TypeInfo *l = infer_type(tc, e->as.binary.left, scope);
                    TypeInfo *r = infer_type(tc, e->as.binary.right, scope);
                    if ((l && l->kind == T_STRING) || (r && r->kind == T_STRING)) return type_new(T_STRING);
                    return l ? l : r;
                }
                case TOK_SLASH: return type_new(T_DECIMAL);
                default: {
                    TypeInfo *l = infer_type(tc, e->as.binary.left, scope);
                    return l ? l : infer_type(tc, e->as.binary.right, scope);
                }
            }
        case E_NEW: {
            if (!find_class(tc, e->as.new_expr.class_name)) return NULL; /* check_expr already reported this */
            TypeInfo *t = type_new(T_CLASS_NAMED);
            t->name = strdup(e->as.new_expr.class_name);
            return t;
        }
        case E_CALL: {
            Expr *callee = e->as.call.callee;
            if (callee->kind == E_IDENT) {
                Sym *sym = scope_find(scope, callee->as.ident);
                return (sym && sym->fn_params) ? sym->fn_return : NULL;
            }
            if (callee->kind == E_MEMBER) {
                TypeInfo *obj_t = infer_type(tc, callee->as.member.object, scope);
                if (obj_t && obj_t->kind == T_CLASS_NAMED) {
                    ClassSig *cs = find_class(tc, obj_t->name);
                    if (cs) {
                        Stmt *method = find_class_member(cs, callee->as.member.name, 1);
                        if (method) return method->as.fun_decl.ret_type;
                    }
                }
            }
            return NULL;
        }
        default: return NULL;
    }
}

static void check_expr(TC *tc, Expr *e, Scope *scope) {
    if (!e) return;
    switch (e->kind) {
        case E_LIT_STRING_INTERP:
            for (StrPart *p = e->as.str_parts; p; p = p->next)
                if (p->is_expr) check_expr(tc, p->expr, scope);
            break;
        case E_IDENT: break;
        case E_BINARY: check_expr(tc, e->as.binary.left, scope); check_expr(tc, e->as.binary.right, scope); break;
        case E_UNARY: check_expr(tc, e->as.unary.operand, scope); break;
        case E_TERNARY:
            check_expr(tc, e->as.ternary.cond, scope);
            check_expr(tc, e->as.ternary.then_e, scope);
            check_expr(tc, e->as.ternary.else_e, scope);
            break;
        case E_BETWEEN:
            check_expr(tc, e->as.between.value, scope);
            check_expr(tc, e->as.between.low, scope);
            check_expr(tc, e->as.between.high, scope);
            break;
        case E_LIST: check_expr_list(tc, e->as.list_items, scope); break;
        case E_DATA:
            for (DataEntry *d = e->as.data_entries; d; d = d->next) check_expr(tc, d->value, scope);
            break;
        case E_FUNC_LIT: {
            Scope *inner = scope_push(scope);
            for (Param *p = e->as.func_lit.params; p; p = p->next) scope_define(inner, p->name, 0, p->type);
            int saved = tc->in_async;
            TypeInfo *saved_ret = tc->cur_return;
            tc->cur_return = e->as.func_lit.ret_type;
            if (e->as.func_lit.is_async) tc->in_async++;
            check_block(tc, e->as.func_lit.body->as.block, inner);
            tc->in_async = saved;
            tc->cur_return = saved_ret;
            break;
        }
        case E_AWAIT:
            /* Legal at top level too, to match live-mode semantics - see
               DESIGN_DECISIONS.md; this build can't know at typecheck time
               whether -live=1 will be passed, so it's never rejected here. */
            check_expr(tc, e->as.await_expr, scope);
            break;
        case E_MEMBER: check_expr(tc, e->as.member.object, scope); break;
        case E_INDEX: check_expr(tc, e->as.index_expr.object, scope); check_expr(tc, e->as.index_expr.index, scope); break;
        case E_ASSIGN: {
            if (e->as.assign.target->kind == E_IDENT) {
                Sym *sym = scope_find(scope, e->as.assign.target->as.ident);
                if (sym && sym->is_fixed)
                    tc_error(tc, e->line, "cannot reassign fixed variable '%s'", e->as.assign.target->as.ident);
                if (sym) {
                    TypeInfo *actual = infer_type(tc, e->as.assign.value, scope);
                    if (types_conflict(sym->type, actual))
                        tc_error(tc, e->line, "cannot assign %s to '%s' (declared %s)", type_name(actual), e->as.assign.target->as.ident, type_name(sym->type));
                }
            }
            check_expr(tc, e->as.assign.target, scope);
            check_expr(tc, e->as.assign.value, scope);
            break;
        }
        case E_NEW: {
            ClassSig *cs = find_class(tc, e->as.new_expr.class_name);
            if (!cs) {
                tc_error(tc, e->line, "'%s' is not a known class", e->as.new_expr.class_name);
            } else {
                Stmt *ctor = find_class_member(cs, "constructor", 1);
                if (ctor) check_call_args(tc, e->as.new_expr.class_name, ctor->as.fun_decl.params, e->as.new_expr.args, scope, e->line);
            }
            check_expr_list(tc, e->as.new_expr.args, scope);
            break;
        }
        case E_CALL: {
            Expr *callee = e->as.call.callee;
            check_expr_list(tc, e->as.call.args, scope);
            if (callee->kind == E_IDENT) {
                Sym *sym = scope_find(scope, callee->as.ident);
                if (sym && sym->fn_params) check_call_args(tc, callee->as.ident, sym->fn_params, e->as.call.args, scope, e->line);
            } else if (callee->kind == E_MEMBER) {
                check_expr(tc, callee->as.member.object, scope);
                if (callee->as.member.object->kind == E_IDENT &&
                    strcmp(callee->as.member.object->as.ident, "live") == 0 &&
                    strcmp(callee->as.member.name, "deg") == 0) {
                    int argc = 0;
                    for (ExprList *a = e->as.call.args; a; a = a->next) argc++;
                    if (argc < 2) tc_error(tc, e->line, "live.deg() requires a type name and at least one value (arity mismatch)");
                }
                TypeInfo *obj_t = infer_type(tc, callee->as.member.object, scope);
                if (obj_t && obj_t->kind == T_CLASS_NAMED) {
                    ClassSig *cs = find_class(tc, obj_t->name);
                    if (cs) {
                        Stmt *method = find_class_member(cs, callee->as.member.name, 1);
                        if (method) check_call_args(tc, callee->as.member.name, method->as.fun_decl.params, e->as.call.args, scope, e->line);
                    }
                }
            } else {
                check_expr(tc, callee, scope);
            }
            break;
        }
        default: break;
    }
}

static void check_block(TC *tc, StmtList *list, Scope *parent) {
    Scope *scope = scope_push(parent);
    /* Hoist function signatures first, mirroring the interpreter's own
       hoist_functions - otherwise a function calling another one defined
       later in the same block would silently skip argument checking
       against it (scope_find would find nothing yet), even though the
       call works fine at runtime. */
    for (StmtList *c = list; c; c = c->next) {
        if (c->item->kind == S_FUN_DECL) {
            Sym *sym = scope_define(scope, c->item->as.fun_decl.name, 1, c->item->as.fun_decl.ret_type);
            sym->fn_params = c->item->as.fun_decl.params;
            sym->fn_return = c->item->as.fun_decl.ret_type;
        }
    }
    for (StmtList *c = list; c; c = c->next) check_stmt(tc, c->item, scope);
}

static void check_stmt(TC *tc, Stmt *s, Scope *scope) {
    switch (s->kind) {
        case S_VARDECL: {
            if (s->as.vardecl.type->kind == T_UNKNOWN)
                tc_error(tc, s->line, "'%s' is missing a required type annotation", s->as.vardecl.name);
            if (s->as.vardecl.init) {
                check_expr(tc, s->as.vardecl.init, scope);
                TypeInfo *actual = infer_type(tc, s->as.vardecl.init, scope);
                if (types_conflict(s->as.vardecl.type, actual))
                    tc_error(tc, s->line, "'%s' declared as %s but initialized with %s",
                             s->as.vardecl.name, type_name(s->as.vardecl.type), type_name(actual));
            }
            scope_define(scope, s->as.vardecl.name, s->as.vardecl.is_fixed, s->as.vardecl.type);
            if (s->as.vardecl.is_fixed && !s->as.vardecl.init)
                tc_error(tc, s->line, "fixed declaration requires an initializer");
            break;
        }
        case S_EXPR: check_expr(tc, s->as.expr_stmt, scope); break;
        case S_IF:
            check_expr(tc, s->as.if_stmt.cond, scope);
            check_stmt(tc, s->as.if_stmt.then_b, scope);
            for (ElifChain *e = s->as.if_stmt.elifs; e; e = e->next) { check_expr(tc, e->cond, scope); check_stmt(tc, e->body, scope); }
            if (s->as.if_stmt.else_b) check_stmt(tc, s->as.if_stmt.else_b, scope);
            break;
        case S_LOOP_WHILE: check_expr(tc, s->as.loop_while.cond, scope); check_stmt(tc, s->as.loop_while.body, scope); break;
        case S_DO_WHILE: check_stmt(tc, s->as.do_while.body, scope); check_expr(tc, s->as.do_while.cond, scope); break;
        case S_FOR_IN: {
            check_expr(tc, s->as.for_in.iterable, scope);
            Scope *inner = scope_push(scope);
            scope_define(inner, s->as.for_in.var_name, 0, NULL);
            if (s->as.for_in.cond) check_expr(tc, s->as.for_in.cond, inner);
            check_stmt(tc, s->as.for_in.body, inner);
            break;
        }
        case S_ITERATE: {
            check_expr(tc, s->as.iterate.collection, scope);
            Scope *inner = scope_push(scope);
            scope_define(inner, s->as.iterate.item_name, 0, NULL);
            check_stmt(tc, s->as.iterate.body, inner);
            break;
        }
        case S_FUN_DECL: {
            /* signature already registered by check_block's hoisting pass;
               just check the body here */
            Scope *inner = scope_push(scope);
            for (Param *p = s->as.fun_decl.params; p; p = p->next) scope_define(inner, p->name, 0, p->type);
            int saved = tc->in_async;
            TypeInfo *saved_ret = tc->cur_return;
            tc->cur_return = s->as.fun_decl.ret_type;
            if (s->as.fun_decl.is_async) tc->in_async++;
            check_block(tc, s->as.fun_decl.body->as.block, inner);
            tc->in_async = saved;
            tc->cur_return = saved_ret;
            break;
        }
        case S_RETURN: {
            if (s->as.return_expr) {
                check_expr(tc, s->as.return_expr, scope);
                TypeInfo *actual = infer_type(tc, s->as.return_expr, scope);
                if (tc->cur_return && types_conflict(tc->cur_return, actual))
                    tc_error(tc, s->line, "return type mismatch: declared %s, returning %s", type_name(tc->cur_return), type_name(actual));
            }
            break;
        }
        case S_BLOCK: check_block(tc, s->as.block, scope); break;
        case S_CLASS: {
            scope_define(scope, s->as.class_decl.name, 1, NULL);
            /* Field/method bodies are checked in their own scope, seeded
               with `this`/`super` as unknown-typed (their precise type
               isn't modeled, so member access through them is never
               flagged - avoids false positives rather than guessing). */
            Scope *inner = scope_push(scope);
            scope_define(inner, "this", 1, NULL);
            scope_define(inner, "super", 1, NULL);
            for (StmtList *m = s->as.class_decl.members; m; m = m->next) check_stmt(tc, m->item, inner);
            break;
        }
        case S_ENUM: scope_define(scope, s->as.enum_decl.name, 1, NULL); break;
        case S_STRUCT: scope_define(scope, s->as.struct_decl.name, 1, NULL); break;
        case S_IMPORT: case S_EXPORT: break; /* module resolution deferred, see DESIGN_DECISIONS.md */
        case S_TRY_CATCH: {
            check_stmt(tc, s->as.try_catch.try_b, scope);
            Scope *inner = scope_push(scope);
            scope_define(inner, s->as.try_catch.err_name, 0, NULL);
            check_stmt(tc, s->as.try_catch.catch_b, inner);
            break;
        }
        case S_BREAK: case S_CONTINUE: break;
    }
}

/* Two passes, mirroring the interpreter's hoist_classes: register every
   class first (so forward references between classes work), then resolve
   each `extends` name to the actual ClassSig now that all names exist. */
static void register_classes(TC *tc, StmtList *list) {
    for (StmtList *c = list; c; c = c->next) {
        if (c->item->kind == S_CLASS) {
            ClassSig *cs = calloc(1, sizeof(ClassSig));
            cs->name = strdup(c->item->as.class_decl.name);
            cs->members = c->item->as.class_decl.members;
            cs->next = tc->classes;
            tc->classes = cs;
        }
    }
    for (StmtList *c = list; c; c = c->next) {
        if (c->item->kind == S_CLASS && c->item->as.class_decl.parent_name) {
            ClassSig *self = find_class(tc, c->item->as.class_decl.name);
            ClassSig *parent = find_class(tc, c->item->as.class_decl.parent_name);
            if (self) self->parent = parent; /* an unresolvable parent is left NULL,
                                                  same tolerant handling as the interpreter */
        }
    }
}

int typecheck_program(StmtList *program, const char *filename) {
    TC tc;
    tc.filename = filename;
    tc.errors = 0;
    tc.in_async = 0;
    tc.cur_return = NULL;
    tc.classes = NULL;
    register_classes(&tc, program);
    Scope *root = scope_push(NULL);
    check_block(&tc, program, root);
    return tc.errors;
}
