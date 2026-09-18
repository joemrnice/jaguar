#define _POSIX_C_SOURCE 200809L
#include "lexer.h"
#include <string.h>
#include <ctype.h>

typedef struct { const char *kw; TokenType t; } KwEntry;

static const KwEntry keywords[] = {
    {"string", TOK_TYPE_STRING}, {"num", TOK_TYPE_NUM}, {"decimal", TOK_TYPE_DECIMAL},
    {"bool", TOK_TYPE_BOOL}, {"scifi", TOK_TYPE_SCIFI}, {"data", TOK_TYPE_DATA},
    {"list", TOK_TYPE_LIST}, {"MixedList", TOK_TYPE_MIXEDLIST}, {"enum", TOK_TYPE_ENUM},
    {"struct", TOK_TYPE_STRUCT}, {"vector", TOK_TYPE_VECTOR}, {"matrix", TOK_TYPE_MATRIX},
    {"Task", TOK_TYPE_TASK}, {"Worker", TOK_TYPE_WORKER}, {"Socket", TOK_TYPE_SOCKET},
    {"var", TOK_VAR}, {"fixed", TOK_FIXED}, {"fun", TOK_FUN}, {"async", TOK_ASYNC},
    {"await", TOK_AWAIT}, {"return", TOK_RETURN}, {"if", TOK_IF}, {"elif", TOK_ELIF},
    {"else", TOK_ELSE}, {"loop", TOK_LOOP}, {"do", TOK_DO}, {"while", TOK_WHILE},
    {"for", TOK_FOR}, {"in", TOK_IN}, {"iterate", TOK_ITERATE}, {"class", TOK_CLASS},
    {"public", TOK_PUBLIC}, {"private", TOK_PRIVATE}, {"import", TOK_IMPORT},
    {"export", TOK_EXPORT}, {"try", TOK_TRY}, {"catch", TOK_CATCH},
    {"new", TOK_NEW}, {"this", TOK_THIS}, {"extends", TOK_EXTENDS}, {"super", TOK_SUPER},
    {"true", TOK_TRUE}, {"false", TOK_FALSE},
    {NULL, TOK_EOF}
};

void lexer_init(Lexer *lx, const char *source, const char *filename) {
    lx->source = source;
    lx->cur = source;
    lx->line = 1;
    lx->filename = filename;
}

static int is_at_end(Lexer *lx) { return *lx->cur == '\0'; }
static char advance(Lexer *lx) { char c = *lx->cur++; if (c == '\n') lx->line++; return c; }
static char peek(Lexer *lx) { return *lx->cur; }
static char peek2(Lexer *lx) { return lx->cur[0] == '\0' ? '\0' : lx->cur[1]; }
static int match(Lexer *lx, char expected) {
    if (is_at_end(lx) || *lx->cur != expected) return 0;
    lx->cur++;
    return 1;
}

static void skip_ws_and_comments(Lexer *lx) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { advance(lx); continue; }
        if (c == '/' && peek2(lx) == '/') {
            while (!is_at_end(lx) && peek(lx) != '\n') advance(lx);
            continue;
        }
        if (c == '/' && peek2(lx) == '*') {
            advance(lx); advance(lx);
            while (!is_at_end(lx) && !(peek(lx) == '*' && peek2(lx) == '/')) advance(lx);
            if (!is_at_end(lx)) { advance(lx); advance(lx); }
            continue;
        }
        break;
    }
}

static Token make(Lexer *lx, TokenType type, const char *start, int len) {
    Token t; t.type = type; t.start = start; t.length = len; t.line = lx->line; return t;
}

static Token identifier_or_keyword(Lexer *lx, const char *start) {
    while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
    int len = (int)(lx->cur - start);
    for (int i = 0; keywords[i].kw; i++) {
        if ((int)strlen(keywords[i].kw) == len && strncmp(keywords[i].kw, start, len) == 0)
            return make(lx, keywords[i].t, start, len);
    }
    return make(lx, TOK_IDENT, start, len);
}

/* Numbers: num (123), decimal (1.5), scifi (3.14E2 / 2.718E0) */
static Token number(Lexer *lx, const char *start) {
    while (isdigit((unsigned char)peek(lx))) advance(lx);
    int is_decimal = 0, is_scifi = 0;
    if (peek(lx) == '.' && isdigit((unsigned char)peek2(lx))) {
        is_decimal = 1;
        advance(lx);
        while (isdigit((unsigned char)peek(lx))) advance(lx);
    }
    if (peek(lx) == 'E' || peek(lx) == 'e') {
        const char *save = lx->cur;
        char c = advance(lx);
        (void)c;
        if (peek(lx) == '+' || peek(lx) == '-') advance(lx);
        if (isdigit((unsigned char)peek(lx))) {
            is_scifi = 1;
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        } else {
            lx->cur = save; /* not an exponent after all */
        }
    }
    int len = (int)(lx->cur - start);
    if (is_scifi) return make(lx, TOK_SCIFI_LIT, start, len);
    if (is_decimal) return make(lx, TOK_DECIMAL_LIT, start, len);
    return make(lx, TOK_NUM_LIT, start, len);
}

/* Strings may contain {{var}}, {expr}, ${} - lexed as one raw literal token;
   the parser's string-interpolation pass splits the raw text into segments. */
static Token string_lit(Lexer *lx) {
    const char *start = lx->cur; /* points after opening quote */
    while (!is_at_end(lx) && peek(lx) != '"') {
        if (peek(lx) == '\\' && peek2(lx) != '\0') advance(lx);
        advance(lx);
    }
    int len = (int)(lx->cur - start);
    if (!is_at_end(lx)) advance(lx); /* closing quote */
    return make(lx, TOK_STRING_LIT, start, len);
}

Token lexer_next(Lexer *lx) {
    skip_ws_and_comments(lx);
    const char *start = lx->cur;
    if (is_at_end(lx)) return make(lx, TOK_EOF, start, 0);

    char c = advance(lx);

    if (isalpha((unsigned char)c) || c == '_') return identifier_or_keyword(lx, start);
    if (isdigit((unsigned char)c)) return number(lx, start);
    if (c == '"') return string_lit(lx);

    switch (c) {
        case '(': return make(lx, TOK_LPAREN, start, 1);
        case ')': return make(lx, TOK_RPAREN, start, 1);
        case '{': return make(lx, TOK_LBRACE, start, 1);
        case '}': return make(lx, TOK_RBRACE, start, 1);
        case '[': return make(lx, TOK_LBRACKET, start, 1);
        case ']': return make(lx, TOK_RBRACKET, start, 1);
        case ';': return make(lx, TOK_SEMI, start, 1);
        case ':': return make(lx, TOK_COLON, start, 1);
        case ',': return make(lx, TOK_COMMA, start, 1);
        case '.': return make(lx, TOK_DOT, start, 1);
        case '?': return make(lx, TOK_QUESTION, start, 1);
        case '+': if (match(lx, '=')) return make(lx, TOK_PLUS_EQ, start, 2);
                  return make(lx, TOK_PLUS, start, 1);
        case '-': if (match(lx, '=')) return make(lx, TOK_MINUS_EQ, start, 2);
                  return make(lx, TOK_MINUS, start, 1);
        case '*':
            if (peek(lx) == '*') {
                advance(lx);
                if (match(lx, '=')) return make(lx, TOK_POW_EQ, start, 3);
                return make(lx, TOK_POW, start, 2);
            }
            if (match(lx, '=')) return make(lx, TOK_STAR_EQ, start, 2);
            return make(lx, TOK_STAR, start, 1);
        case '/': if (match(lx, '=')) return make(lx, TOK_SLASH_EQ, start, 2);
                  return make(lx, TOK_SLASH, start, 1);
        case '%': if (match(lx, '=')) return make(lx, TOK_PERCENT_EQ, start, 2);
                  return make(lx, TOK_PERCENT, start, 1);
        case '=': if (match(lx, '=')) return make(lx, TOK_EQ, start, 2);
                  return make(lx, TOK_ASSIGN, start, 1);
        case '!': if (match(lx, '=')) return make(lx, TOK_NE, start, 2);
                  return make(lx, TOK_NOT, start, 1);
        case '&': if (match(lx, '&')) return make(lx, TOK_AND, start, 2);
                  break;
        case '|': if (match(lx, '|')) return make(lx, TOK_OR, start, 2);
                  break;
        case '>':
            if (peek(lx) == '=') { advance(lx); return make(lx, TOK_GE, start, 2); }
            if (peek(lx) == '>') { advance(lx); return make(lx, TOK_SHR, start, 2); }
            return make(lx, TOK_GT, start, 1);
        case '<':
            if (peek(lx) == '=') { advance(lx); return make(lx, TOK_LE, start, 2); }
            if (peek(lx) == '<' && lx->cur[1] == '<') {
                advance(lx); advance(lx);
                return make(lx, TOK_BETWEEN, start, 3);
            }
            if (peek(lx) == '<') { advance(lx); return make(lx, TOK_SHL, start, 2); }
            return make(lx, TOK_LT, start, 1);
        default: break;
    }
    return make(lx, TOK_ERROR, start, 1);
}

const char *token_type_name(TokenType t) {
    switch (t) {
        case TOK_EOF: return "EOF";
        case TOK_ERROR: return "ERROR";
        case TOK_NUM_LIT: return "NUM_LIT";
        case TOK_DECIMAL_LIT: return "DECIMAL_LIT";
        case TOK_SCIFI_LIT: return "SCIFI_LIT";
        case TOK_STRING_LIT: return "STRING_LIT";
        case TOK_IDENT: return "IDENT";
        default: return "TOKEN";
    }
}
