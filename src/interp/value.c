#define _POSIX_C_SOURCE 200809L
#include "value.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

Value *value_new(ValueKind kind) {
    Value *v = calloc(1, sizeof(Value));
    v->kind = kind;
    return v;
}

Value *value_num(long long n) { Value *v = value_new(V_NUM); v->as.num = n; return v; }
Value *value_decimal(double d) { Value *v = value_new(V_DECIMAL); v->as.decimal = d; return v; }
Value *value_scifi(double d) { Value *v = value_new(V_SCIFI); v->as.scifi = d; return v; }
Value *value_bool(int b) { Value *v = value_new(V_BOOL); v->as.boolean = b; return v; }
Value *value_string(const char *s) { Value *v = value_new(V_STRING); v->as.string = strdup(s ? s : ""); return v; }
Value *value_void(void) { return value_new(V_VOID); }
Value *value_list_empty(void) { Value *v = value_new(V_LIST); v->as.list = NULL; return v; }
Value *value_data_empty(void) { Value *v = value_new(V_DATA); v->as.data = NULL; return v; }

void value_list_append(Value *list, Value *item) {
    ListNode *node = calloc(1, sizeof(ListNode));
    node->item = item;
    if (!list->as.list) { list->as.list = node; return; }
    ListNode *cur = list->as.list;
    while (cur->next) cur = cur->next;
    cur->next = node;
}

void value_list_insert_front(Value *list, Value *item) {
    ListNode *node = calloc(1, sizeof(ListNode));
    node->item = item;
    node->next = list->as.list;
    list->as.list = node;
}

int value_list_length(Value *list) {
    int n = 0;
    for (ListNode *c = list->as.list; c; c = c->next) n++;
    return n;
}

Value *value_data_get(Value *data, const char *key) {
    for (DataNode *c = data->as.data; c; c = c->next)
        if (strcmp(c->key, key) == 0) return c->val;
    return NULL;
}

void value_data_set(Value *data, const char *key, Value *val) {
    for (DataNode *c = data->as.data; c; c = c->next) {
        if (strcmp(c->key, key) == 0) { c->val = val; return; }
    }
    DataNode *node = calloc(1, sizeof(DataNode));
    node->key = strdup(key);
    node->val = val;
    node->next = data->as.data;
    data->as.data = node;
}

double value_as_number(Value *v) {
    switch (v->kind) {
        case V_NUM: return (double)v->as.num;
        case V_DECIMAL: return v->as.decimal;
        case V_SCIFI: return v->as.scifi;
        case V_BOOL: return v->as.boolean ? 1 : 0;
        default: return 0;
    }
}

int value_truthy(Value *v) {
    switch (v->kind) {
        case V_BOOL: return v->as.boolean;
        case V_NUM: return v->as.num != 0;
        case V_DECIMAL: return v->as.decimal != 0;
        case V_SCIFI: return v->as.scifi != 0;
        case V_STRING: return v->as.string && v->as.string[0] != '\0';
        case V_VOID: return 0;
        case V_LIST: return v->as.list != NULL;
        case V_DATA: return v->as.data != NULL;
        default: return 1;
    }
}

int value_equal(Value *a, Value *b) {
    if (a->kind == V_STRING && b->kind == V_STRING) return strcmp(a->as.string, b->as.string) == 0;
    if ((a->kind == V_NUM || a->kind == V_DECIMAL || a->kind == V_SCIFI || a->kind == V_BOOL) &&
        (b->kind == V_NUM || b->kind == V_DECIMAL || b->kind == V_SCIFI || b->kind == V_BOOL))
        return value_as_number(a) == value_as_number(b);
    if (a->kind == V_VOID && b->kind == V_VOID) return 1;
    return a == b;
}

char *value_to_display_string(Value *v) {
    char buf[512];
    switch (v->kind) {
        case V_STRING: return strdup(v->as.string);
        case V_NUM: snprintf(buf, sizeof(buf), "%lld", v->as.num); return strdup(buf);
        case V_DECIMAL: snprintf(buf, sizeof(buf), "%g", v->as.decimal); return strdup(buf);
        case V_SCIFI: snprintf(buf, sizeof(buf), "%g", v->as.scifi); return strdup(buf);
        case V_BOOL: return strdup(v->as.boolean ? "true" : "false");
        case V_VOID: return strdup("void");
        case V_FUNC: return strdup("<function>");
        case V_CLASS: {
            ClassInfo *ci = (ClassInfo *)v->as.native;
            char buf2[256];
            snprintf(buf2, sizeof(buf2), "<class %s>", ci->name);
            return strdup(buf2);
        }
        case V_INSTANCE: {
            Instance *inst = (Instance *)v->as.native;
            char *fields_s = value_to_display_string(inst->fields);
            size_t need = strlen(inst->class_info->name) + strlen(fields_s) + 4;
            char *out = malloc(need);
            snprintf(out, need, "%s %s", inst->class_info->name, fields_s);
            free(fields_s);
            return out;
        }
        case V_LIST: {
            size_t cap = 128, len = 0;
            char *out = malloc(cap);
            out[0] = '\0';
            strcat(out, "[");
            len = 1;
            int first = 1;
            for (ListNode *c = v->as.list; c; c = c->next) {
                char *item_s = value_to_display_string(c->item);
                size_t need = strlen(item_s) + 4;
                while (len + need > cap) { cap *= 2; out = realloc(out, cap); }
                if (!first) { strcat(out, ", "); len += 2; }
                strcat(out, item_s);
                len += strlen(item_s);
                free(item_s);
                first = 0;
            }
            strcat(out, "]");
            return out;
        }
        case V_DATA: {
            size_t cap = 128, len = 0;
            char *out = malloc(cap);
            out[0] = '\0';
            strcat(out, "{");
            len = 1;
            int first = 1;
            for (DataNode *c = v->as.data; c; c = c->next) {
                char *item_s = value_to_display_string(c->val);
                size_t need = strlen(item_s) + strlen(c->key) + 8;
                while (len + need > cap) { cap *= 2; out = realloc(out, cap); }
                if (!first) { strcat(out, ", "); len += 2; }
                strcat(out, "\""); strcat(out, c->key); strcat(out, "\": ");
                strcat(out, item_s);
                len += strlen(item_s) + strlen(c->key) + 4;
                free(item_s);
                first = 0;
            }
            strcat(out, "}");
            return out;
        }
        default: return strdup("<native>");
    }
}

Value *value_copy(Value *v) {
    /* Jaguar value-semantics copy (used e.g. when passing values across worker
       boundaries conceptually, and for list/data literal construction). */
    switch (v->kind) {
        case V_LIST: {
            Value *out = value_list_empty();
            for (ListNode *c = v->as.list; c; c = c->next) value_list_append(out, value_copy(c->item));
            return out;
        }
        case V_DATA: {
            Value *out = value_data_empty();
            /* preserve insertion order by walking reversed */
            DataNode *rev = NULL;
            for (DataNode *c = v->as.data; c; c = c->next) {
                DataNode *n = calloc(1, sizeof(DataNode));
                n->key = c->key; n->val = c->val; n->next = rev; rev = n;
            }
            for (DataNode *c = rev; c; c = c->next) value_data_set(out, c->key, value_copy(c->val));
            return out;
        }
        default: return v; /* scalars/functions are shared; fine for a tree-walker */
    }
}

/* ---------------- environment ---------------- */
Env *env_new(Env *parent) {
    Env *e = calloc(1, sizeof(Env));
    e->parent = parent;
    return e;
}

void env_define(Env *env, const char *name, Value *value, int is_fixed) {
    EnvEntry *entry = calloc(1, sizeof(EnvEntry));
    entry->name = strdup(name);
    entry->value = value;
    entry->is_fixed = is_fixed;
    entry->next = env->entries;
    env->entries = entry;
}

static EnvEntry *env_find(Env *env, const char *name) {
    for (Env *e = env; e; e = e->parent)
        for (EnvEntry *c = e->entries; c; c = c->next)
            if (strcmp(c->name, name) == 0) return c;
    return NULL;
}

Value *env_get(Env *env, const char *name) {
    EnvEntry *e = env_find(env, name);
    return e ? e->value : NULL;
}

int env_set(Env *env, const char *name, Value *value) {
    EnvEntry *e = env_find(env, name);
    if (!e || e->is_fixed) return 0;
    e->value = value;
    return 1;
}

int env_is_fixed(Env *env, const char *name) {
    EnvEntry *e = env_find(env, name);
    return e ? e->is_fixed : 0;
}
