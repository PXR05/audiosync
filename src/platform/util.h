#ifndef UTIL_H
#define UTIL_H
#include "platform/compat.h"
#include <string.h>

static inline int text_equal_ci(const char *a, const char *b) {
    return !strcmp(a, b) || !_stricmp(a, b);
}

wchar_t *utf8_to_wide(const char *s);
char *wide_to_utf8(const wchar_t *ws);
int mkdirs_w(const wchar_t *path);
int file_size_w(const wchar_t *path, unsigned long long *out);
void sleep_ms(int ms);
char *str_dup(const char *s);

#endif
