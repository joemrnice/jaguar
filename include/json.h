#ifndef JAG_JSON_H
#define JAG_JSON_H
#include "value.h"

Value *json_parse(const char *text);
/* Returns a malloc'd string; caller frees. */
char *json_stringify(Value *v);

#endif
