#ifndef JAG_DIAGNOSTICS_H
#define JAG_DIAGNOSTICS_H

#include <stdio.h>
#include <stdbool.h>

typedef enum {
    DIAG_SEV_ERROR,
    DIAG_SEV_WARNING,
    DIAG_SEV_INFO,
    DIAG_SEV_HINT
} DiagnosticSeverity;

typedef struct {
    DiagnosticSeverity severity;
    const char *code;      /* e.g. "JAG101" */
    const char *message;
    const char *filename;
    int start_line;
    int start_col;
    int end_line;
    int end_col;
    const char *source;    /* e.g. "parser", "typechecker", "linter" */
    const char *help;      /* optional help suggestion */
} Diagnostic;

typedef struct DiagnosticNode {
    Diagnostic diag;
    struct DiagnosticNode *next;
} DiagnosticNode;

typedef struct {
    DiagnosticNode *head;
    DiagnosticNode *tail;
    int count;
    int error_count;
    int warning_count;
} DiagnosticBag;

void diag_bag_init(DiagnosticBag *bag);
void diag_bag_clear(DiagnosticBag *bag);
void diag_add(DiagnosticBag *bag, DiagnosticSeverity severity, const char *code, const char *message,
              const char *filename, int start_line, int start_col, int end_line, int end_col,
              const char *source, const char *help);

void diag_print_terminal(const DiagnosticBag *bag, const char *source_text);
void diag_print_json(const DiagnosticBag *bag, FILE *out);

/* Global diagnostic bag for current compilation/checking session */
extern DiagnosticBag g_diag_bag;

#endif
