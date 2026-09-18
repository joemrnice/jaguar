#define _POSIX_C_SOURCE 200809L
#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const char *json_skip_ws(const char *s) { while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++; return s; }
static Value *json_parse_value(const char **sp);

static char *json_parse_string_raw(const char **sp) {
    const char *s = *sp;
    if (*s != '"') return strdup("");
    s++;
    size_t cap = 64, len = 0;
    char *out = malloc(cap);
    while (*s && *s != '"') {
        char c = *s++;
        if (c == '\\' && *s) {
            char n = *s++;
            switch (n) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case '"': c = '"'; break;
                case '\\': c = '\\'; break;
                case '/': c = '/'; break;
                default: c = n; break;
            }
        }
        if (len + 1 >= cap) { cap *= 2; out = realloc(out, cap); }
        out[len++] = c;
    }
    out[len] = '\0';
    if (*s == '"') s++;
    *sp = s;
    return out;
}

static Value *json_parse_value(const char **sp) {
    const char *s = json_skip_ws(*sp);
    if (*s == '"') { char *str = json_parse_string_raw(&s); Value *v = value_string(str); free(str); *sp = s; return v; }
    if (*s == '{') {
        s++;
        Value *obj = value_data_empty();
        s = json_skip_ws(s);
        if (*s == '}') { s++; *sp = s; return obj; }
        for (;;) {
            s = json_skip_ws(s);
            char *key = json_parse_string_raw(&s);
            s = json_skip_ws(s);
            if (*s == ':') s++;
            Value *val = json_parse_value(&s);
            value_data_set(obj, key, val);
            free(key);
            s = json_skip_ws(s);
            if (*s == ',') { s++; continue; }
            if (*s == '}') { s++; break; }
            break;
        }
        *sp = s;
        return obj;
    }
    if (*s == '[') {
        s++;
        Value *list = value_list_empty();
        s = json_skip_ws(s);
        if (*s == ']') { s++; *sp = s; return list; }
        for (;;) {
            Value *item = json_parse_value(&s);
            value_list_append(list, item);
            s = json_skip_ws(s);
            if (*s == ',') { s++; continue; }
            if (*s == ']') { s++; break; }
            break;
        }
        *sp = s;
        return list;
    }
    if (strncmp(s, "true", 4) == 0) { *sp = s + 4; return value_bool(1); }
    if (strncmp(s, "false", 5) == 0) { *sp = s + 5; return value_bool(0); }
    if (strncmp(s, "null", 4) == 0) { *sp = s + 4; return value_void(); }
    /* number */
    const char *start = s;
    int is_float = 0;
    if (*s == '-') s++;
    while (isdigit((unsigned char)*s)) s++;
    if (*s == '.') { is_float = 1; s++; while (isdigit((unsigned char)*s)) s++; }
    if (*s == 'e' || *s == 'E') { is_float = 1; s++; if (*s == '+' || *s == '-') s++; while (isdigit((unsigned char)*s)) s++; }
    char numbuf[64];
    int nlen = (int)(s - start);
    if (nlen >= (int)sizeof(numbuf)) nlen = sizeof(numbuf) - 1;
    memcpy(numbuf, start, nlen);
    numbuf[nlen] = '\0';
    *sp = s;
    if (is_float) return value_decimal(atof(numbuf));
    return value_num(atoll(numbuf));
}

Value *json_parse(const char *text) {
    const char *s = text;
    return json_parse_value(&s);
}

static void json_stringify_into(Value *v, char **out, size_t *cap, size_t *len);
static void json_append(char **out, size_t *cap, size_t *len, const char *s) {
    size_t n = strlen(s);
    while (*len + n + 1 > *cap) { *cap *= 2; *out = realloc(*out, *cap); }
    memcpy(*out + *len, s, n + 1);
    *len += n;
}

static void json_stringify_into(Value *v, char **out, size_t *cap, size_t *len) {
    char buf[64];
    switch (v->kind) {
        case V_STRING: {
            json_append(out, cap, len, "\"");
            for (char *c = v->as.string; *c; c++) {
                if (*c == '"' || *c == '\\') { char esc[3] = {'\\', *c, 0}; json_append(out, cap, len, esc); }
                else if (*c == '\n') json_append(out, cap, len, "\\n");
                else { char one[2] = {*c, 0}; json_append(out, cap, len, one); }
            }
            json_append(out, cap, len, "\"");
            break;
        }
        case V_NUM: snprintf(buf, sizeof(buf), "%lld", v->as.num); json_append(out, cap, len, buf); break;
        case V_DECIMAL: snprintf(buf, sizeof(buf), "%g", v->as.decimal); json_append(out, cap, len, buf); break;
        case V_SCIFI: snprintf(buf, sizeof(buf), "%g", v->as.scifi); json_append(out, cap, len, buf); break;
        case V_BOOL: json_append(out, cap, len, v->as.boolean ? "true" : "false"); break;
        case V_VOID: json_append(out, cap, len, "null"); break;
        case V_LIST: {
            json_append(out, cap, len, "[");
            int first = 1;
            for (ListNode *c = v->as.list; c; c = c->next) {
                if (!first) json_append(out, cap, len, ",");
                json_stringify_into(c->item, out, cap, len);
                first = 0;
            }
            json_append(out, cap, len, "]");
            break;
        }
        case V_DATA: {
            json_append(out, cap, len, "{");
            int first = 1;
            /* reverse to preserve insertion order */
            DataNode *rev = NULL;
            for (DataNode *c = v->as.data; c; c = c->next) {
                DataNode *n = malloc(sizeof(DataNode)); n->key = c->key; n->val = c->val; n->next = rev; rev = n;
            }
            for (DataNode *c = rev; c; c = c->next) {
                if (!first) json_append(out, cap, len, ",");
                json_append(out, cap, len, "\"");
                json_append(out, cap, len, c->key);
                json_append(out, cap, len, "\":");
                json_stringify_into(c->val, out, cap, len);
                first = 0;
            }
            json_append(out, cap, len, "}");
            break;
        }
        default: json_append(out, cap, len, "null"); break;
    }
}

char *json_stringify(Value *v) {
    size_t cap = 128, len = 0;
    char *out = malloc(cap);
    out[0] = '\0';
    json_stringify_into(v, &out, &cap, &len);
    return out;
}
