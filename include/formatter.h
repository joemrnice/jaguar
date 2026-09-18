#ifndef JAG_FORMATTER_H
#define JAG_FORMATTER_H

#include <stdio.h>

typedef struct {
    int indent_width;
    int use_tabs;
} FormatterConfig;

char *format_jaguar_source(const char *source, FormatterConfig config);
int jag_fmt_file(const char *filepath, int check_only, int write_in_place, FormatterConfig config);

#endif
