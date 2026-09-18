#define _POSIX_C_SOURCE 200809L
#include "formatter.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

char *format_jaguar_source(const char *source, FormatterConfig config) {
    if (!source) return strdup("");
    int indent_width = config.indent_width > 0 ? config.indent_width : 4;

    size_t cap = strlen(source) * 2 + 256;
    char *out = malloc(cap);
    out[0] = '\0';
    size_t out_len = 0;

    int indent_level = 0;
    const char *p = source;

    while (*p) {
        /* skip leading whitespace of current line */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        /* read line till '\n' */
        const char *line_start = p;
        while (*p && *p != '\n') p++;
        int line_len = (int)(p - line_start);
        if (*p == '\n') p++;

        /* trim trailing whitespace */
        while (line_len > 0 && (line_start[line_len - 1] == ' ' || line_start[line_len - 1] == '\t' || line_start[line_len - 1] == '\r')) {
            line_len--;
        }

        if (line_len == 0) {
            /* empty line */
            if (out_len + 2 > cap) { cap *= 2; out = realloc(out, cap); }
            out[out_len++] = '\n';
            out[out_len] = '\0';
            continue;
        }

        /* Check for closing braces at line start to adjust indent before printing */
        int leading_close = 0;
        for (int i = 0; i < line_len; i++) {
            if (line_start[i] == '}') leading_close++;
            else if (!isspace((unsigned char)line_start[i])) break;
        }

        int eff_indent = indent_level - leading_close;
        if (eff_indent < 0) eff_indent = 0;

        /* append indent */
        int spaces = eff_indent * indent_width;
        while (out_len + spaces + line_len + 2 > cap) { cap *= 2; out = realloc(out, cap); }
        for (int s = 0; s < spaces; s++) {
            out[out_len++] = config.use_tabs ? '\t' : ' ';
        }

        /* append line content */
        memcpy(out + out_len, line_start, line_len);
        out_len += line_len;
        out[out_len++] = '\n';
        out[out_len] = '\0';

        /* count net brace changes to update indent_level for subsequent lines */
        int net_braces = 0;
        int in_str = 0;
        for (int i = 0; i < line_len; i++) {
            if (line_start[i] == '"' && (i == 0 || line_start[i - 1] != '\\')) {
                in_str = !in_str;
            }
            if (!in_str) {
                if (line_start[i] == '{') net_braces++;
                if (line_start[i] == '}') net_braces--;
            }
        }
        indent_level += net_braces;
        if (indent_level < 0) indent_level = 0;
    }

    return out;
}

int jag_fmt_file(const char *filepath, int check_only, int write_in_place, FormatterConfig config) {
    FILE *f = fopen(filepath, "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(len + 1);
    size_t n = fread(buf, 1, len, f);
    buf[n] = '\0';
    fclose(f);

    char *formatted = format_jaguar_source(buf, config);
    int is_diff = strcmp(buf, formatted) != 0;

    if (check_only) {
        if (is_diff) printf("%s: requires formatting\n", filepath);
        free(buf);
        free(formatted);
        return is_diff ? 1 : 0;
    }

    if (write_in_place && is_diff) {
        FILE *wf = fopen(filepath, "wb");
        if (wf) {
            fputs(formatted, wf);
            fclose(wf);
            printf("%s: formatted\n", filepath);
        }
    } else if (!write_in_place) {
        fputs(formatted, stdout);
    }

    free(buf);
    free(formatted);
    return 0;
}
