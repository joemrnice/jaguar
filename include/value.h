#ifndef JAG_VALUE_H
#define JAG_VALUE_H
#include "ast.h"

typedef enum {
    V_NUM, V_DECIMAL, V_SCIFI, V_BOOL, V_STRING, V_LIST, V_DATA, V_FUNC, V_VOID, V_NATIVE,
    V_CLASS, V_INSTANCE
} ValueKind;

typedef struct Value Value;
typedef struct Env Env;

typedef struct ListNode { Value *item; struct ListNode *next; } ListNode;
typedef struct DataNode { char *key; Value *val; struct DataNode *next; } DataNode;

typedef struct {
    Param *params;
    Stmt *body;
    Env *closure;
    int is_async;
    char *name;
} FuncValue;

/* OOP: `class X [extends Y] { ... }` declarations and `new X(...)` instances.
   `members` is the class body's StmtList as parsed - S_VARDECL entries are
   fields, S_FUN_DECL entries are methods (including a method literally
   named "constructor", called by `new`/`super(...)` by convention - see
   the language guide). */
typedef struct ClassInfo {
    char *name;
    char *parent_name;         /* from `extends`, or NULL */
    struct ClassInfo *parent;   /* resolved once all classes are hoisted */
    StmtList *members;
    Env *closure;                /* defining scope, for methods' free variables */
} ClassInfo;

typedef struct Instance {
    ClassInfo *class_info;   /* the most-derived class this was `new`'d as */
    Value *fields;             /* a V_DATA value holding field name -> Value* */
} Instance;

struct Value {
    ValueKind kind;
    union {
        long long num;
        double decimal;
        double scifi;
        int boolean;
        char *string;
        ListNode *list;      /* head; NULL = empty */
        DataNode *data;      /* head; NULL = empty */
        FuncValue *func;
        void *native;
    } as;
};

Value *value_new(ValueKind kind);
Value *value_num(long long n);
Value *value_decimal(double d);
Value *value_scifi(double d);
Value *value_bool(int b);
Value *value_string(const char *s);
Value *value_void(void);
Value *value_list_empty(void);
Value *value_data_empty(void);
void value_list_append(Value *list, Value *item);
void value_list_insert_front(Value *list, Value *item);
int value_list_length(Value *list);
Value *value_data_get(Value *data, const char *key);
void value_data_set(Value *data, const char *key, Value *val);
char *value_to_display_string(Value *v);
int value_truthy(Value *v);
int value_equal(Value *a, Value *b);
double value_as_number(Value *v);
Value *value_copy(Value *v);

/* ---- environment ---- */
typedef struct EnvEntry { char *name; Value *value; int is_fixed; struct EnvEntry *next; } EnvEntry;
struct Env {
    EnvEntry *entries;
    Env *parent;
};

Env *env_new(Env *parent);
void env_define(Env *env, const char *name, Value *value, int is_fixed);
Value *env_get(Env *env, const char *name);
int env_set(Env *env, const char *name, Value *value); /* returns 0 if not found or fixed */
int env_is_fixed(Env *env, const char *name);

#endif
