#ifndef JAG_AST_H
#define JAG_AST_H

typedef enum {
    T_STRING, T_NUM, T_DECIMAL, T_BOOL, T_SCIFI, T_DATA, T_LIST, T_MIXEDLIST,
    T_ENUM, T_STRUCT, T_VECTOR, T_MATRIX, T_TASK, T_WORKER, T_SOCKET,
    T_VOID, T_UNKNOWN, T_CLASS_NAMED
} JagType;

typedef struct TypeInfo {
    JagType kind;
    struct TypeInfo *elem;   /* for list<T>, vector<T>, Task<T> */
    char *name;              /* for T_CLASS_NAMED or struct/enum name */
} TypeInfo;

typedef enum {
    E_LIT_NUM, E_LIT_DECIMAL, E_LIT_SCIFI, E_LIT_BOOL, E_LIT_STRING_INTERP,
    E_IDENT, E_BINARY, E_UNARY, E_ASSIGN, E_CALL, E_MEMBER, E_INDEX,
    E_TERNARY, E_BETWEEN, E_LIST, E_DATA, E_FUNC_LIT, E_AWAIT, E_NEW
} ExprKind;

typedef enum {
    S_VARDECL, S_EXPR, S_IF, S_LOOP_WHILE, S_DO_WHILE, S_FOR_IN, S_ITERATE,
    S_FUN_DECL, S_RETURN, S_BLOCK, S_CLASS, S_ENUM, S_STRUCT, S_IMPORT,
    S_EXPORT, S_TRY_CATCH, S_BREAK, S_CONTINUE
} StmtKind;

typedef struct Expr Expr;
typedef struct Stmt Stmt;

/* string interpolation: sequence of literal / expr parts */
typedef struct StrPart {
    int is_expr;
    char *literal;      /* if !is_expr */
    Expr *expr;          /* if is_expr */
    struct StrPart *next;
} StrPart;

typedef struct ExprList { Expr *item; struct ExprList *next; } ExprList;
typedef struct DataEntry { char *key; Expr *value; struct DataEntry *next; } DataEntry;
typedef struct Param { char *name; TypeInfo *type; struct Param *next; } Param;

struct Expr {
    ExprKind kind;
    int line;
    union {
        long long num_lit;
        double decimal_lit;
        double scifi_lit;
        int bool_lit;
        StrPart *str_parts;
        char *ident;
        struct { Expr *left; Expr *right; int op; } binary;
        struct { Expr *operand; int op; } unary;
        struct { Expr *target; Expr *value; int op; } assign; /* op: 0=plain,else augmented */
        struct { Expr *callee; ExprList *args; } call;
        struct { Expr *object; char *name; } member;
        struct { Expr *object; Expr *index; } index_expr;
        struct { Expr *cond; Expr *then_e; Expr *else_e; } ternary;
        struct { Expr *value; Expr *low; Expr *high; } between;
        ExprList *list_items;
        DataEntry *data_entries;
        struct { Param *params; TypeInfo *ret_type; Stmt *body; int is_async; } func_lit;
        Expr *await_expr;
        struct { char *class_name; ExprList *args; } new_expr;
    } as;
};

typedef struct StmtList { Stmt *item; struct StmtList *next; } StmtList;
typedef struct ElifChain { Expr *cond; Stmt *body; struct ElifChain *next; } ElifChain;

struct Stmt {
    StmtKind kind;
    int line;
    union {
        struct { char *name; TypeInfo *type; Expr *init; int is_fixed; } vardecl;
        Expr *expr_stmt;
        struct { Expr *cond; Stmt *then_b; ElifChain *elifs; Stmt *else_b; } if_stmt;
        struct { Expr *cond; Stmt *body; } loop_while;
        struct { Expr *cond; Stmt *body; } do_while;
        struct { char *var_name; Expr *iterable; Expr *cond; Stmt *body; } for_in;
        struct { Expr *collection; char *item_name; Stmt *body; } iterate;
        struct { char *name; Param *params; TypeInfo *ret_type; Stmt *body; int is_async; } fun_decl;
        Expr *return_expr;
        StmtList *block;
        struct { char *name; StmtList *members; int visibility; char *parent_name; } class_decl; /* 0=default 1=public 2=private */
        struct { char *name; char **values; int count; } enum_decl;
        struct { char *name; Param *fields; } struct_decl;
        char *import_path;
        char *export_name;
        struct { Stmt *try_b; char *err_name; Stmt *catch_b; } try_catch;
    } as;
};

TypeInfo *type_new(JagType kind);
Expr *expr_new(ExprKind kind, int line);
Stmt *stmt_new(StmtKind kind, int line);

#endif
