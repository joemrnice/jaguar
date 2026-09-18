#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    Lexer lx;
    Token cur;
    Token prev;
    const char *filename;
    int had_error;
} Parser;

static void advance_p(Parser *p) {
    p->prev = p->cur;
    p->cur = lexer_next(&p->lx);
}

static void init_parser(Parser *p, const char *source, const char *filename) {
    lexer_init(&p->lx, source, filename);
    p->filename = filename;
    p->had_error = 0;
    advance_p(p); /* prime cur, prev garbage */
}

static char *tok_str(Token t) {
    char *s = malloc(t.length + 1);
    memcpy(s, t.start, t.length);
    s[t.length] = '\0';
    return s;
}

static void error_at(Parser *p, Token t, const char *msg) {
    fprintf(stderr, "%s:%d: parse error near '%.*s': %s\n",
            p->filename ? p->filename : "<input>", t.line, t.length, t.start, msg);
    p->had_error = 1;
}

static int check(Parser *p, TokenType t) { return p->cur.type == t; }

static int match_t(Parser *p, TokenType t) {
    if (!check(p, t)) return 0;
    advance_p(p);
    return 1;
}

static void expect(Parser *p, TokenType t, const char *what) {
    if (check(p, t)) { advance_p(p); return; }
    error_at(p, p->cur, what);
    /* best-effort recovery: don't advance past EOF */
    if (!check(p, TOK_EOF)) advance_p(p);
}

/* ---------- forward decls ---------- */
static Expr *parse_expr(Parser *p);
static Expr *parse_assignment(Parser *p);
static Stmt *parse_statement(Parser *p);
static Stmt *parse_block(Parser *p);
static TypeInfo *parse_type(Parser *p);
static Expr *parse_string_literal_token(Parser *p, Token t);

/* ---------- type parsing ---------- */
static TypeInfo *parse_type(Parser *p) {
    JagType kind = T_UNKNOWN;
    switch (p->cur.type) {
        case TOK_TYPE_STRING: kind = T_STRING; break;
        case TOK_TYPE_NUM: kind = T_NUM; break;
        case TOK_TYPE_DECIMAL: kind = T_DECIMAL; break;
        case TOK_TYPE_BOOL: kind = T_BOOL; break;
        case TOK_TYPE_SCIFI: kind = T_SCIFI; break;
        case TOK_TYPE_DATA: kind = T_DATA; break;
        case TOK_TYPE_LIST: kind = T_LIST; break;
        case TOK_TYPE_MIXEDLIST: kind = T_MIXEDLIST; break;
        case TOK_TYPE_ENUM: kind = T_ENUM; break;
        case TOK_TYPE_STRUCT: kind = T_STRUCT; break;
        case TOK_TYPE_VECTOR: kind = T_VECTOR; break;
        case TOK_TYPE_MATRIX: kind = T_MATRIX; break;
        case TOK_TYPE_TASK: kind = T_TASK; break;
        case TOK_TYPE_WORKER: kind = T_WORKER; break;
        case TOK_TYPE_SOCKET: kind = T_SOCKET; break;
        case TOK_IDENT: {
            TypeInfo *t = type_new(T_CLASS_NAMED);
            t->name = tok_str(p->cur);
            advance_p(p);
            return t;
        }
        default:
            error_at(p, p->cur, "expected a type");
            advance_p(p);
            return type_new(T_UNKNOWN);
    }
    advance_p(p);
    TypeInfo *t = type_new(kind);
    if (match_t(p, TOK_LT)) { /* generic: list<num>, Task<data>, vector<num> */
        t->elem = parse_type(p);
        expect(p, TOK_GT, "expected '>' to close generic type");
    }
    return t;
}

/* ---------- string interpolation ---------- */
/* Splits raw string-literal text on {{expr}}, {expr}, ${expr} into StrPart list.
   Escapes: \" \\ \n \t are honored in literal segments. */
static char *unescape_segment(const char *start, int len) {
    char *out = malloc(len + 1);
    int j = 0;
    for (int i = 0; i < len; i++) {
        char c = start[i];
        if (c == '\\' && i + 1 < len) {
            char n = start[++i];
            switch (n) {
                case 'n': out[j++] = '\n'; break;
                case 't': out[j++] = '\t'; break;
                case '"': out[j++] = '"'; break;
                case '\\': out[j++] = '\\'; break;
                default: out[j++] = n; break;
            }
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
    return out;
}

static Expr *parse_expr_substring(const char *src, int len, const char *filename, int line) {
    char *buf = malloc(len + 1);
    memcpy(buf, src, len);
    buf[len] = '\0';
    Parser sub;
    init_parser(&sub, buf, filename);
    Expr *e = parse_expr(&sub);
    if (e) e->line = line;
    /* buf intentionally leaked (arena-style for AST lifetime); freed at process exit */
    return e;
}

static Expr *parse_string_literal_token(Parser *p, Token t) {
    Expr *e = expr_new(E_LIT_STRING_INTERP, t.line);
    StrPart *head = NULL, *tail = NULL;
    const char *s = t.start;
    int len = t.length;
    int i = 0, lit_start = 0;
    while (i < len) {
        /* Design decision (see DESIGN_DECISIONS.md): only {{expr}} and ${expr}
           are treated as interpolation openers. A bare single '{' is literal
           text (e.g. inside embedded JSON), which the original single-brace
           `{expr}` form from the spec would otherwise collide with. */
        int is_dbl = (i + 1 < len && s[i] == '{' && s[i + 1] == '{');
        int is_dollar_brace = (i + 1 < len && s[i] == '$' && s[i + 1] == '{');
        if (is_dbl || is_dollar_brace) {
            if (i > lit_start) {
                StrPart *part = calloc(1, sizeof(StrPart));
                part->is_expr = 0;
                part->literal = unescape_segment(s + lit_start, i - lit_start);
                if (!head) head = tail = part; else { tail->next = part; tail = part; }
            }
            int open_len = is_dbl ? 2 : 1;
            int start_expr = i + open_len;
            int j = start_expr;
            int depth = 1;
            while (j < len && depth > 0) {
                if (s[j] == '{') depth++;
                else if (s[j] == '}') { depth--; if (depth == 0) break; }
                j++;
            }
            int expr_len = j - start_expr;
            StrPart *part = calloc(1, sizeof(StrPart));
            part->is_expr = 1;
            part->expr = parse_expr_substring(s + start_expr, expr_len, p->filename, t.line);
            if (!head) head = tail = part; else { tail->next = part; tail = part; }
            /* consume closing brace(s) */
            j++; /* skip first '}' */
            if (is_dbl && j < len && s[j] == '}') j++;
            i = j;
            lit_start = i;
        } else {
            i++;
        }
    }
    if (lit_start < len) {
        StrPart *part = calloc(1, sizeof(StrPart));
        part->is_expr = 0;
        part->literal = unescape_segment(s + lit_start, len - lit_start);
        if (!head) head = tail = part; else { tail->next = part; tail = part; }
    }
    if (!head) {
        head = calloc(1, sizeof(StrPart));
        head->is_expr = 0;
        head->literal = strdup("");
    }
    e->as.str_parts = head;
    return e;
}

/* ---------- expressions ---------- */
static ExprList *parse_arg_list(Parser *p) {
    ExprList *head = NULL, *tail = NULL;
    if (!check(p, TOK_RPAREN)) {
        do {
            Expr *a = parse_assignment(p);
            ExprList *node = calloc(1, sizeof(ExprList));
            node->item = a;
            if (!head) head = tail = node; else { tail->next = node; tail = node; }
        } while (match_t(p, TOK_COMMA));
    }
    return head;
}

static Expr *parse_primary(Parser *p) {
    Token t = p->cur;
    if (match_t(p, TOK_NUM_LIT)) {
        Expr *e = expr_new(E_LIT_NUM, t.line);
        char *s = tok_str(t); e->as.num_lit = atoll(s); free(s);
        return e;
    }
    if (match_t(p, TOK_DECIMAL_LIT)) {
        Expr *e = expr_new(E_LIT_DECIMAL, t.line);
        char *s = tok_str(t); e->as.decimal_lit = atof(s); free(s);
        return e;
    }
    if (match_t(p, TOK_SCIFI_LIT)) {
        Expr *e = expr_new(E_LIT_SCIFI, t.line);
        char *s = tok_str(t); e->as.scifi_lit = atof(s); free(s);
        return e;
    }
    if (match_t(p, TOK_TRUE)) { Expr *e = expr_new(E_LIT_BOOL, t.line); e->as.bool_lit = 1; return e; }
    if (match_t(p, TOK_FALSE)) { Expr *e = expr_new(E_LIT_BOOL, t.line); e->as.bool_lit = 0; return e; }
    if (match_t(p, TOK_STRING_LIT)) return parse_string_literal_token(p, t);
    if (match_t(p, TOK_IDENT)) {
        Expr *e = expr_new(E_IDENT, t.line);
        e->as.ident = tok_str(t);
        return e;
    }
    if (match_t(p, TOK_TYPE_TASK)) {
        /* "Task" doubles as the Task.all(...) namespace in expression
           position, even though it's also a type keyword (Task<T>). */
        Expr *e = expr_new(E_IDENT, t.line);
        e->as.ident = strdup("Task");
        return e;
    }
    if (match_t(p, TOK_THIS)) {
        Expr *e = expr_new(E_IDENT, t.line);
        e->as.ident = strdup("this");
        return e;
    }
    if (match_t(p, TOK_SUPER)) {
        Expr *e = expr_new(E_IDENT, t.line);
        e->as.ident = strdup("super");
        return e;
    }
    if (match_t(p, TOK_NEW)) {
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected a class name after 'new'");
        char *cname = tok_str(nm);
        expect(p, TOK_LPAREN, "expected '(' after class name");
        ExprList *args = parse_arg_list(p);
        expect(p, TOK_RPAREN, "expected ')' after constructor arguments");
        Expr *e = expr_new(E_NEW, t.line);
        e->as.new_expr.class_name = cname;
        e->as.new_expr.args = args;
        return e;
    }
    if (match_t(p, TOK_AWAIT)) {
        Expr *e = expr_new(E_AWAIT, t.line);
        e->as.await_expr = parse_assignment(p);
        return e;
    }
    if (match_t(p, TOK_FUN) || (check(p, TOK_ASYNC))) {
        int is_async = match_t(p, TOK_ASYNC);
        if (is_async) expect(p, TOK_FUN, "expected 'fun' after 'async'");
        expect(p, TOK_LPAREN, "expected '(' in function literal");
        Param *phead = NULL, *ptail = NULL;
        if (!check(p, TOK_RPAREN)) {
            do {
                Token nm = p->cur;
                expect(p, TOK_IDENT, "expected parameter name");
                Param *prm = calloc(1, sizeof(Param));
                prm->name = tok_str(nm);
                if (match_t(p, TOK_COLON)) prm->type = parse_type(p);
                else prm->type = type_new(T_UNKNOWN);
                if (!phead) phead = ptail = prm; else { ptail->next = prm; ptail = prm; }
            } while (match_t(p, TOK_COMMA));
        }
        expect(p, TOK_RPAREN, "expected ')' after parameters");
        TypeInfo *ret = type_new(T_VOID);
        if (match_t(p, TOK_COLON)) ret = parse_type(p);
        Stmt *body = parse_block(p);
        Expr *e = expr_new(E_FUNC_LIT, t.line);
        e->as.func_lit.params = phead;
        e->as.func_lit.ret_type = ret;
        e->as.func_lit.body = body;
        e->as.func_lit.is_async = is_async;
        return e;
    }
    if (match_t(p, TOK_LPAREN)) {
        Expr *e = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        return e;
    }
    if (match_t(p, TOK_LBRACKET)) {
        Expr *e = expr_new(E_LIST, t.line);
        ExprList *head = NULL, *tail = NULL;
        if (!check(p, TOK_RBRACKET)) {
            do {
                Expr *item = parse_assignment(p);
                ExprList *node = calloc(1, sizeof(ExprList));
                node->item = item;
                if (!head) head = tail = node; else { tail->next = node; tail = node; }
            } while (match_t(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACKET, "expected ']'");
        e->as.list_items = head;
        return e;
    }
    if (match_t(p, TOK_LBRACE)) {
        Expr *e = expr_new(E_DATA, t.line);
        DataEntry *head = NULL, *tail = NULL;
        if (!check(p, TOK_RBRACE)) {
            do {
                Token key = p->cur;
                char *keystr;
                if (check(p, TOK_STRING_LIT)) { advance_p(p); keystr = tok_str(key); }
                else { expect(p, TOK_IDENT, "expected key"); keystr = tok_str(key); }
                expect(p, TOK_COLON, "expected ':' after key");
                Expr *val = parse_assignment(p);
                DataEntry *node = calloc(1, sizeof(DataEntry));
                node->key = keystr;
                node->value = val;
                if (!head) head = tail = node; else { tail->next = node; tail = node; }
            } while (match_t(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACE, "expected '}'");
        e->as.data_entries = head;
        return e;
    }
    error_at(p, t, "expected an expression");
    if (!check(p, TOK_EOF)) advance_p(p);
    return expr_new(E_LIT_BOOL, t.line);
}

/* True for any token whose lexeme can be used as a member/method name
   right after '.' - i.e. TOK_IDENT plus every keyword, since keywords
   like `catch`, `await`, `in` etc. are also legitimate runtime method
   names (Task.catch(), Task.await(), ...) and there's no ambiguity in
   this position - only a name can follow '.'. */
static int is_member_name_token(TokenType t) {
    switch (t) {
        case TOK_IDENT:
        case TOK_TYPE_STRING: case TOK_TYPE_NUM: case TOK_TYPE_DECIMAL:
        case TOK_TYPE_BOOL: case TOK_TYPE_SCIFI: case TOK_TYPE_DATA:
        case TOK_TYPE_LIST: case TOK_TYPE_MIXEDLIST: case TOK_TYPE_ENUM:
        case TOK_TYPE_STRUCT: case TOK_TYPE_VECTOR: case TOK_TYPE_MATRIX:
        case TOK_TYPE_TASK: case TOK_TYPE_WORKER: case TOK_TYPE_SOCKET:
        case TOK_VAR: case TOK_FIXED: case TOK_FUN: case TOK_ASYNC:
        case TOK_AWAIT: case TOK_RETURN: case TOK_IF: case TOK_ELIF:
        case TOK_ELSE: case TOK_LOOP: case TOK_DO: case TOK_WHILE:
        case TOK_FOR: case TOK_IN: case TOK_ITERATE: case TOK_CLASS:
        case TOK_PUBLIC: case TOK_PRIVATE: case TOK_IMPORT: case TOK_EXPORT:
        case TOK_TRY: case TOK_CATCH: case TOK_LIVE:
        case TOK_TRUE: case TOK_FALSE:
            return 1;
        default:
            return 0;
    }
}

static Expr *parse_postfix(Parser *p) {
    Expr *e = parse_primary(p);
    for (;;) {
        if (match_t(p, TOK_DOT)) {
            Token nm = p->cur;
            if (is_member_name_token(nm.type)) advance_p(p);
            else expect(p, TOK_IDENT, "expected member name after '.'");
            char *name = tok_str(nm);
            if (check(p, TOK_LPAREN)) {
                advance_p(p);
                ExprList *args = parse_arg_list(p);
                expect(p, TOK_RPAREN, "expected ')' after arguments");
                Expr *member = expr_new(E_MEMBER, e->line);
                member->as.member.object = e;
                member->as.member.name = name;
                Expr *call = expr_new(E_CALL, e->line);
                call->as.call.callee = member;
                call->as.call.args = args;
                e = call;
            } else {
                Expr *member = expr_new(E_MEMBER, e->line);
                member->as.member.object = e;
                member->as.member.name = name;
                e = member;
            }
        } else if (match_t(p, TOK_LBRACKET)) {
            Expr *idx = parse_expr(p);
            expect(p, TOK_RBRACKET, "expected ']'");
            Expr *ie = expr_new(E_INDEX, e->line);
            ie->as.index_expr.object = e;
            ie->as.index_expr.index = idx;
            e = ie;
        } else if (check(p, TOK_LPAREN)) {
            advance_p(p);
            ExprList *args = parse_arg_list(p);
            expect(p, TOK_RPAREN, "expected ')' after arguments");
            Expr *call = expr_new(E_CALL, e->line);
            call->as.call.callee = e;
            call->as.call.args = args;
            e = call;
        } else break;
    }
    return e;
}

static Expr *parse_unary(Parser *p) {
    if (check(p, TOK_NOT) || check(p, TOK_MINUS)) {
        Token op = p->cur;
        advance_p(p);
        Expr *operand = parse_unary(p);
        Expr *e = expr_new(E_UNARY, op.line);
        e->as.unary.operand = operand;
        e->as.unary.op = op.type;
        return e;
    }
    return parse_postfix(p);
}

static Expr *parse_power(Parser *p) {
    Expr *left = parse_unary(p);
    if (match_t(p, TOK_POW)) {
        Expr *right = parse_power(p); /* right-assoc */
        Expr *e = expr_new(E_BINARY, left->line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = TOK_POW;
        return e;
    }
    return left;
}

static Expr *parse_mul(Parser *p) {
    Expr *left = parse_power(p);
    while (check(p, TOK_STAR) || check(p, TOK_SLASH) || check(p, TOK_PERCENT)) {
        Token op = p->cur; advance_p(p);
        Expr *right = parse_power(p);
        Expr *e = expr_new(E_BINARY, op.line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = op.type;
        left = e;
    }
    return left;
}

static Expr *parse_add(Parser *p) {
    Expr *left = parse_mul(p);
    while (check(p, TOK_PLUS) || check(p, TOK_MINUS)) {
        Token op = p->cur; advance_p(p);
        Expr *right = parse_mul(p);
        Expr *e = expr_new(E_BINARY, op.line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = op.type;
        left = e;
    }
    return left;
}

static Expr *parse_shift(Parser *p) {
    Expr *left = parse_add(p);
    while (check(p, TOK_SHL) || check(p, TOK_SHR)) {
        Token op = p->cur; advance_p(p);
        Expr *right = parse_add(p);
        Expr *e = expr_new(E_BINARY, op.line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = op.type;
        left = e;
    }
    return left;
}

static Expr *parse_between(Parser *p) {
    Expr *left = parse_shift(p);
    if (match_t(p, TOK_BETWEEN)) {
        expect(p, TOK_LPAREN, "expected '(' after '<<<'");
        Expr *low = parse_assignment(p);
        expect(p, TOK_COMMA, "expected ',' between low and high bounds");
        Expr *high = parse_assignment(p);
        expect(p, TOK_RPAREN, "expected ')' after bounds");
        Expr *e = expr_new(E_BETWEEN, left->line);
        e->as.between.value = left; e->as.between.low = low; e->as.between.high = high;
        return e;
    }
    return left;
}

static Expr *parse_comparison(Parser *p) {
    Expr *left = parse_between(p);
    while (check(p, TOK_GT) || check(p, TOK_LT) || check(p, TOK_GE) || check(p, TOK_LE)) {
        Token op = p->cur; advance_p(p);
        Expr *right = parse_between(p);
        Expr *e = expr_new(E_BINARY, op.line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = op.type;
        left = e;
    }
    return left;
}

static Expr *parse_equality(Parser *p) {
    Expr *left = parse_comparison(p);
    while (check(p, TOK_EQ) || check(p, TOK_NE)) {
        Token op = p->cur; advance_p(p);
        Expr *right = parse_comparison(p);
        Expr *e = expr_new(E_BINARY, op.line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = op.type;
        left = e;
    }
    return left;
}

static Expr *parse_and(Parser *p) {
    Expr *left = parse_equality(p);
    while (match_t(p, TOK_AND)) {
        Expr *right = parse_equality(p);
        Expr *e = expr_new(E_BINARY, left->line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = TOK_AND;
        left = e;
    }
    return left;
}

static Expr *parse_or(Parser *p) {
    Expr *left = parse_and(p);
    while (match_t(p, TOK_OR)) {
        Expr *right = parse_and(p);
        Expr *e = expr_new(E_BINARY, left->line);
        e->as.binary.left = left; e->as.binary.right = right; e->as.binary.op = TOK_OR;
        left = e;
    }
    return left;
}

static Expr *parse_ternary(Parser *p) {
    Expr *cond = parse_or(p);
    if (match_t(p, TOK_QUESTION)) {
        Expr *then_e = parse_assignment(p);
        expect(p, TOK_COLON, "expected ':' in ternary expression");
        Expr *else_e = parse_assignment(p);
        Expr *e = expr_new(E_TERNARY, cond->line);
        e->as.ternary.cond = cond; e->as.ternary.then_e = then_e; e->as.ternary.else_e = else_e;
        return e;
    }
    return cond;
}

static int is_assign_op(TokenType t) {
    return t == TOK_ASSIGN || t == TOK_PLUS_EQ || t == TOK_MINUS_EQ ||
           t == TOK_STAR_EQ || t == TOK_SLASH_EQ || t == TOK_PERCENT_EQ || t == TOK_POW_EQ;
}

static Expr *parse_assignment(Parser *p) {
    Expr *left = parse_ternary(p);
    if (is_assign_op(p->cur.type)) {
        Token op = p->cur; advance_p(p);
        Expr *value = parse_assignment(p);
        Expr *e = expr_new(E_ASSIGN, op.line);
        e->as.assign.target = left; e->as.assign.value = value; e->as.assign.op = op.type;
        return e;
    }
    return left;
}

static Expr *parse_expr(Parser *p) { return parse_assignment(p); }

/* ---------- statements ---------- */
static Stmt *parse_block(Parser *p) {
    Stmt *s = stmt_new(S_BLOCK, p->cur.line);
    expect(p, TOK_LBRACE, "expected '{'");
    StmtList *head = NULL, *tail = NULL;
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        Stmt *st = parse_statement(p);
        if (st) {
            StmtList *node = calloc(1, sizeof(StmtList));
            node->item = st;
            if (!head) head = tail = node; else { tail->next = node; tail = node; }
        }
    }
    expect(p, TOK_RBRACE, "expected '}'");
    s->as.block = head;
    return s;
}

static Stmt *parse_var_or_fixed(Parser *p, int is_fixed) {
    int line = p->cur.line;
    Token nm = p->cur;
    expect(p, TOK_IDENT, "expected variable name");
    Stmt *s = stmt_new(S_VARDECL, line);
    s->as.vardecl.name = tok_str(nm);
    s->as.vardecl.is_fixed = is_fixed;
    if (match_t(p, TOK_COLON)) s->as.vardecl.type = parse_type(p);
    else s->as.vardecl.type = type_new(T_UNKNOWN);
    if (match_t(p, TOK_ASSIGN)) s->as.vardecl.init = parse_expr(p);
    else s->as.vardecl.init = NULL;
    expect(p, TOK_SEMI, "expected ';' after declaration");
    return s;
}

static Param *parse_param_list(Parser *p) {
    Param *head = NULL, *tail = NULL;
    if (!check(p, TOK_RPAREN)) {
        do {
            Token nm = p->cur;
            expect(p, TOK_IDENT, "expected parameter name");
            Param *prm = calloc(1, sizeof(Param));
            prm->name = tok_str(nm);
            if (match_t(p, TOK_COLON)) prm->type = parse_type(p);
            else prm->type = type_new(T_UNKNOWN);
            if (!head) head = tail = prm; else { tail->next = prm; tail = prm; }
        } while (match_t(p, TOK_COMMA));
    }
    return head;
}

static Stmt *parse_fun_decl(Parser *p, int is_async) {
    int line = p->cur.line;
    Token nm = p->cur;
    expect(p, TOK_IDENT, "expected function name");
    expect(p, TOK_LPAREN, "expected '(' after function name");
    Param *params = parse_param_list(p);
    expect(p, TOK_RPAREN, "expected ')' after parameters");
    TypeInfo *ret = type_new(T_VOID);
    if (match_t(p, TOK_COLON)) ret = parse_type(p);
    Stmt *body = parse_block(p);
    Stmt *s = stmt_new(S_FUN_DECL, line);
    s->as.fun_decl.name = tok_str(nm);
    s->as.fun_decl.params = params;
    s->as.fun_decl.ret_type = ret;
    s->as.fun_decl.body = body;
    s->as.fun_decl.is_async = is_async;
    return s;
}

static Stmt *parse_if(Parser *p) {
    int line = p->cur.line;
    expect(p, TOK_LPAREN, "expected '(' after 'if'");
    Expr *cond = parse_expr(p);
    expect(p, TOK_RPAREN, "expected ')' after condition");
    Stmt *then_b = parse_block(p);
    ElifChain *ehead = NULL, *etail = NULL;
    while (check(p, TOK_ELIF)) {
        advance_p(p);
        expect(p, TOK_LPAREN, "expected '(' after 'elif'");
        Expr *econd = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')' after condition");
        Stmt *ebody = parse_block(p);
        ElifChain *node = calloc(1, sizeof(ElifChain));
        node->cond = econd; node->body = ebody;
        if (!ehead) ehead = etail = node; else { etail->next = node; etail = node; }
    }
    Stmt *else_b = NULL;
    if (match_t(p, TOK_ELSE)) else_b = parse_block(p);
    Stmt *s = stmt_new(S_IF, line);
    s->as.if_stmt.cond = cond;
    s->as.if_stmt.then_b = then_b;
    s->as.if_stmt.elifs = ehead;
    s->as.if_stmt.else_b = else_b;
    return s;
}

static Stmt *parse_statement(Parser *p) {
    int line = p->cur.line;
    if (match_t(p, TOK_VAR)) return parse_var_or_fixed(p, 0);
    if (match_t(p, TOK_FIXED)) return parse_var_or_fixed(p, 1);
    if (match_t(p, TOK_IF)) return parse_if(p);
    if (match_t(p, TOK_ASYNC)) { expect(p, TOK_FUN, "expected 'fun' after 'async'"); return parse_fun_decl(p, 1); }
    if (match_t(p, TOK_FUN)) return parse_fun_decl(p, 0);
    if (match_t(p, TOK_RETURN)) {
        Expr *val = NULL;
        if (!check(p, TOK_SEMI)) val = parse_expr(p);
        expect(p, TOK_SEMI, "expected ';' after return value");
        Stmt *s = stmt_new(S_RETURN, line);
        s->as.return_expr = val;
        return s;
    }
    if (match_t(p, TOK_LOOP)) {
        expect(p, TOK_LPAREN, "expected '(' after 'loop'");
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        Stmt *body = parse_block(p);
        Stmt *s = stmt_new(S_LOOP_WHILE, line);
        s->as.loop_while.cond = cond; s->as.loop_while.body = body;
        return s;
    }
    if (match_t(p, TOK_DO)) {
        expect(p, TOK_LOOP, "expected 'loop' after 'do'");
        Stmt *body = parse_block(p);
        expect(p, TOK_WHILE, "expected 'while' after do-loop body");
        expect(p, TOK_LPAREN, "expected '(' after 'while'");
        Expr *cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        expect(p, TOK_SEMI, "expected ';' after do-while");
        Stmt *s = stmt_new(S_DO_WHILE, line);
        s->as.do_while.cond = cond; s->as.do_while.body = body;
        return s;
    }
    if (match_t(p, TOK_FOR)) {
        expect(p, TOK_LPAREN, "expected '(' after 'for'");
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected loop variable name");
        expect(p, TOK_IN, "expected 'in' in for-in loop");
        Expr *iterable = parse_expr(p);
        Expr *cond = NULL;
        if (match_t(p, TOK_COMMA)) cond = parse_expr(p);
        expect(p, TOK_RPAREN, "expected ')'");
        Stmt *body = parse_block(p);
        Stmt *s = stmt_new(S_FOR_IN, line);
        s->as.for_in.var_name = tok_str(nm);
        s->as.for_in.iterable = iterable;
        s->as.for_in.cond = cond;
        s->as.for_in.body = body;
        return s;
    }
    if (match_t(p, TOK_ITERATE)) {
        expect(p, TOK_LPAREN, "expected '(' after 'iterate'");
        Expr *coll = parse_expr(p);
        expect(p, TOK_COMMA, "expected ',' before item name");
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected item variable name");
        expect(p, TOK_RPAREN, "expected ')'");
        Stmt *body = parse_block(p);
        Stmt *s = stmt_new(S_ITERATE, line);
        s->as.iterate.collection = coll;
        s->as.iterate.item_name = tok_str(nm);
        s->as.iterate.body = body;
        return s;
    }
    if (check(p, TOK_CLASS) || check(p, TOK_PUBLIC) || check(p, TOK_PRIVATE)) {
        int vis = 0;
        if (match_t(p, TOK_PUBLIC)) vis = 1;
        else if (match_t(p, TOK_PRIVATE)) vis = 2;
        expect(p, TOK_CLASS, "expected 'class'");
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected class name");
        char *parent_name = NULL;
        if (match_t(p, TOK_EXTENDS)) {
            Token pnm = p->cur;
            expect(p, TOK_IDENT, "expected parent class name after 'extends'");
            parent_name = tok_str(pnm);
        }
        expect(p, TOK_LBRACE, "expected '{'");
        StmtList *head = NULL, *tail = NULL;
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            Stmt *member;
            if (match_t(p, TOK_VAR)) member = parse_var_or_fixed(p, 0);
            else if (match_t(p, TOK_FIXED)) member = parse_var_or_fixed(p, 1);
            else if (match_t(p, TOK_FUN)) member = parse_fun_decl(p, 0);
            else { error_at(p, p->cur, "expected class member"); advance_p(p); continue; }
            StmtList *node = calloc(1, sizeof(StmtList));
            node->item = member;
            if (!head) head = tail = node; else { tail->next = node; tail = node; }
        }
        expect(p, TOK_RBRACE, "expected '}'");
        Stmt *s = stmt_new(S_CLASS, line);
        s->as.class_decl.name = tok_str(nm);
        s->as.class_decl.members = head;
        s->as.class_decl.visibility = vis;
        s->as.class_decl.parent_name = parent_name;
        return s;
    }
    if (match_t(p, TOK_TYPE_ENUM)) {
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected enum name");
        expect(p, TOK_LBRACE, "expected '{'");
        char **vals = malloc(sizeof(char*) * 64);
        int count = 0;
        if (!check(p, TOK_RBRACE)) {
            do {
                Token v = p->cur;
                expect(p, TOK_IDENT, "expected enum value");
                if (count < 64) vals[count++] = tok_str(v);
            } while (match_t(p, TOK_COMMA));
        }
        expect(p, TOK_RBRACE, "expected '}'");
        Stmt *s = stmt_new(S_ENUM, line);
        s->as.enum_decl.name = tok_str(nm);
        s->as.enum_decl.values = vals;
        s->as.enum_decl.count = count;
        return s;
    }
    if (match_t(p, TOK_TYPE_STRUCT)) {
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected struct name");
        expect(p, TOK_LBRACE, "expected '{'");
        Param *head = NULL, *tail = NULL;
        while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
            Token fnm = p->cur;
            expect(p, TOK_IDENT, "expected field name");
            expect(p, TOK_COLON, "expected ':' after field name");
            TypeInfo *ft = parse_type(p);
            expect(p, TOK_SEMI, "expected ';' after field");
            Param *prm = calloc(1, sizeof(Param));
            prm->name = tok_str(fnm);
            prm->type = ft;
            if (!head) head = tail = prm; else { tail->next = prm; tail = prm; }
        }
        expect(p, TOK_RBRACE, "expected '}'");
        Stmt *s = stmt_new(S_STRUCT, line);
        s->as.struct_decl.name = tok_str(nm);
        s->as.struct_decl.fields = head;
        return s;
    }
    if (check(p, TOK_TYPE_VECTOR) || check(p, TOK_TYPE_MATRIX)) {
        /* vector<num> name = [...]; matrix<num> name = [[...]]; */
        TypeInfo *t = parse_type(p);
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected variable name");
        Stmt *s = stmt_new(S_VARDECL, line);
        s->as.vardecl.name = tok_str(nm);
        s->as.vardecl.type = t;
        s->as.vardecl.is_fixed = 0;
        if (match_t(p, TOK_ASSIGN)) s->as.vardecl.init = parse_expr(p);
        expect(p, TOK_SEMI, "expected ';'");
        return s;
    }
    if (match_t(p, TOK_IMPORT)) {
        /* supports both: import "path.jag";  and  import data from "path.jag"; */
        if (check(p, TOK_IDENT)) {
            Token nm = p->cur;
            if (nm.length == 4 && strncmp(nm.start, "from", 4) != 0) {
                /* bound name before 'from' - consume name then require 'from' as ident */
                advance_p(p);
                if (check(p, TOK_IDENT) && p->cur.length == 4 && strncmp(p->cur.start, "from", 4) == 0) {
                    advance_p(p);
                } else {
                    error_at(p, p->cur, "expected 'from' in import statement");
                }
            }
        }
        Token path = p->cur;
        expect(p, TOK_STRING_LIT, "expected import path string");
        expect(p, TOK_SEMI, "expected ';' after import");
        Stmt *s = stmt_new(S_IMPORT, line);
        s->as.import_path = tok_str(path);
        return s;
    }
    if (match_t(p, TOK_EXPORT)) {
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected identifier after 'export'");
        expect(p, TOK_SEMI, "expected ';' after export");
        Stmt *s = stmt_new(S_EXPORT, line);
        s->as.export_name = tok_str(nm);
        return s;
    }
    if (match_t(p, TOK_TRY)) {
        Stmt *try_b = parse_block(p);
        expect(p, TOK_CATCH, "expected 'catch' after try block");
        expect(p, TOK_LPAREN, "expected '(' after 'catch'");
        Token nm = p->cur;
        expect(p, TOK_IDENT, "expected error variable name");
        char *err_name = tok_str(nm);
        if (match_t(p, TOK_COLON)) parse_type(p); /* type checked at typecheck stage */
        expect(p, TOK_RPAREN, "expected ')'");
        Stmt *catch_b = parse_block(p);
        Stmt *s = stmt_new(S_TRY_CATCH, line);
        s->as.try_catch.try_b = try_b;
        s->as.try_catch.err_name = err_name;
        s->as.try_catch.catch_b = catch_b;
        return s;
    }
    if (check(p, TOK_LBRACE)) return parse_block(p);

    Expr *e = parse_expr(p);
    expect(p, TOK_SEMI, "expected ';' after expression");
    Stmt *s = stmt_new(S_EXPR, line);
    s->as.expr_stmt = e;
    return s;
}

ParseResult parse_program(const char *source, const char *filename) {
    Parser p;
    init_parser(&p, source, filename);
    StmtList *head = NULL, *tail = NULL;
    while (!check(&p, TOK_EOF)) {
        Stmt *s = parse_statement(&p);
        if (s) {
            StmtList *node = calloc(1, sizeof(StmtList));
            node->item = s;
            if (!head) head = tail = node; else { tail->next = node; tail = node; }
        }
        if (p.had_error) break;
    }
    ParseResult r;
    r.stmts = head;
    r.had_error = p.had_error;
    return r;
}
