#define _POSIX_C_SOURCE 200809L
#include "diagnostics.h"
#include <stdlib.h>
#include <string.h>

DiagnosticBag g_diag_bag;

void diag_bag_init(DiagnosticBag *bag) {
    if (!bag) return;
    bag->head = NULL;
    bag->tail = NULL;
    bag->count = 0;
    bag->error_count = 0;
    bag->warning_count = 0;
}

void diag_bag_clear(DiagnosticBag *bag) {
    if (!bag) return;
    DiagnosticNode *curr = bag->head;
    while (curr) {
        DiagnosticNode *next = curr->next;
        free((void*)curr->diag.code);
        free((void*)curr->diag.message);
        free((void*)curr->diag.filename);
        free((void*)curr->diag.source);
        if (curr->diag.help) free((void*)curr->diag.help);
        free(curr);
        curr = next;
    }
    bag->head = NULL;
    bag->tail = NULL;
    bag->count = 0;
    bag->error_count = 0;
    bag->warning_count = 0;
}

static char *safe_strdup(const char *s) {
    if (!s) return NULL;
    return strdup(s);
}

void diag_add(DiagnosticBag *bag, DiagnosticSeverity severity, const char *code, const char *message,
              const char *filename, int start_line, int start_col, int end_line, int end_col,
              const char *source, const char *help) {
    if (!bag) bag = &g_diag_bag;
    DiagnosticNode *node = (DiagnosticNode*)malloc(sizeof(DiagnosticNode));
    node->diag.severity = severity;
    node->diag.code = safe_strdup(code ? code : "JAG000");
    node->diag.message = safe_strdup(message ? message : "");
    node->diag.filename = safe_strdup(filename ? filename : "<stdin>");
    node->diag.start_line = start_line > 0 ? start_line : 1;
    node->diag.start_col = start_col > 0 ? start_col : 1;
    node->diag.end_line = end_line >= start_line ? end_line : node->diag.start_line;
    node->diag.end_col = end_col >= start_col ? end_col : node->diag.start_col;
    node->diag.source = safe_strdup(source ? source : "compiler");
    node->diag.help = safe_strdup(help);
    node->next = NULL;

    if (!bag->head) {
        bag->head = node;
        bag->tail = node;
    } else {
        bag->tail->next = node;
        bag->tail = node;
    }
    bag->count++;
    if (severity == DIAG_SEV_ERROR) bag->error_count++;
    else if (severity == DIAG_SEV_WARNING) bag->warning_count++;
}

static const char *sev_str(DiagnosticSeverity sev) {
    switch (sev) {
        case DIAG_SEV_ERROR: return "error";
        case DIAG_SEV_WARNING: return "warning";
        case DIAG_SEV_INFO: return "info";
        case DIAG_SEV_HINT: return "hint";
    }
    return "error";
}

void diag_print_terminal(const DiagnosticBag *bag, const char *source_text) {
    (void)source_text;
    if (!bag) bag = &g_diag_bag;
    DiagnosticNode *curr = bag->head;
    while (curr) {
        Diagnostic *d = &curr->diag;
        fprintf(stderr, "%s:%d:%d: [%s %s] %s\n",
                d->filename, d->start_line, d->start_col,
                sev_str(d->severity), d->code, d->message);
        if (d->help) {
            fprintf(stderr, "  help: %s\n", d->help);
        }
        curr = curr->next;
    }
}

static void print_json_escaped(FILE *out, const char *str) {
    if (!str) { fprintf(out, "\"\""); return; }
    fputc('"', out);
    for (const char *p = str; *p; p++) {
        switch (*p) {
            case '"': fputs("\\\"", out); break;
            case '\\': fputs("\\\\", out); break;
            case '\b': fputs("\\b", out); break;
            case '\f': fputs("\\f", out); break;
            case '\n': fputs("\\n", out); break;
            case '\r': fputs("\\r", out); break;
            case '\t': fputs("\\t", out); break;
            default:
                if ((unsigned char)*p < 0x20) {
                    fprintf(out, "\\u%04x", (unsigned char)*p);
                } else {
                    fputc(*p, out);
                }
                break;
        }
    }
    fputc('"', out);
}

void diag_print_json(const DiagnosticBag *bag, FILE *out) {
    if (!bag) bag = &g_diag_bag;
    if (!out) out = stdout;

    fprintf(out, "{\n  \"diagnostics\": [\n");
    DiagnosticNode *curr = bag->head;
    while (curr) {
        Diagnostic *d = &curr->diag;
        fprintf(out, "    {\n");
        fprintf(out, "      \"severity\": "); print_json_escaped(out, sev_str(d->severity)); fprintf(out, ",\n");
        fprintf(out, "      \"code\": "); print_json_escaped(out, d->code); fprintf(out, ",\n");
        fprintf(out, "      \"message\": "); print_json_escaped(out, d->message); fprintf(out, ",\n");
        fprintf(out, "      \"filename\": "); print_json_escaped(out, d->filename); fprintf(out, ",\n");
        fprintf(out, "      \"range\": {\n");
        fprintf(out, "        \"start\": { \"line\": %d, \"column\": %d },\n", d->start_line, d->start_col);
        fprintf(out, "        \"end\": { \"line\": %d, \"column\": %d }\n", d->end_line, d->end_col);
        fprintf(out, "      },\n");
        fprintf(out, "      \"source\": "); print_json_escaped(out, d->source);
        if (d->help) {
            fprintf(out, ",\n      \"help\": "); print_json_escaped(out, d->help);
        }
        fprintf(out, "\n    }%s\n", curr->next ? "," : "");
        curr = curr->next;
    }
    fprintf(out, "  ],\n  \"errorCount\": %d,\n  \"warningCount\": %d\n}\n",
            bag->error_count, bag->warning_count);
}
