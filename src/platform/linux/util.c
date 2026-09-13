#ifndef _WIN32
#define _GNU_SOURCE
#include "platform/util.h"
#include <errno.h>
#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

wchar_t *utf8_to_wide(const char *text) {
    if (!text)
        return NULL;
    setlocale(LC_CTYPE, "");
    size_t length = mbstowcs(NULL, text, 0);
    if (length == (size_t)-1)
        return NULL;
    wchar_t *wide = malloc((length + 1) * sizeof *wide);
    if (wide)
        mbstowcs(wide, text, length + 1);
    return wide;
}

char *wide_to_utf8(const wchar_t *wide) {
    if (!wide)
        return NULL;
    setlocale(LC_CTYPE, "");
    size_t length = wcstombs(NULL, wide, 0);
    if (length == (size_t)-1)
        return NULL;
    char *text = malloc(length + 1);
    if (text)
        wcstombs(text, wide, length + 1);
    return text;
}

int mkdirs_w(const wchar_t *wide) {
    char *path = wide_to_utf8(wide);
    if (!path || !*path) {
        free(path);
        return -1;
    }
    for (char *p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
    for (char *p = path + 1; *p; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = 0;
            if (mkdir(path, 0700) != 0 && errno != EEXIST) {
                free(path);
                return -1;
            }
            *p = saved;
        }
    }
    int result = mkdir(path, 0700);
    if (result != 0 && errno == EEXIST)
        result = 0;
    free(path);
    return result;
}

int file_size_w(const wchar_t *wide, unsigned long long *out) {
    char *path = wide_to_utf8(wide);
    struct stat info;
    if (path)
        for (char *p = path; *p; ++p)
            if (*p == '\\')
                *p = '/';
    int result = path && stat(path, &info) == 0 && S_ISREG(info.st_mode) ? 0 : -1;
    if (!result)
        *out = (unsigned long long)info.st_size;
    free(path);
    return result;
}

void sleep_ms(int milliseconds) {
    if (milliseconds < 0)
        milliseconds = 0;
    struct timespec delay = {milliseconds / 1000, (milliseconds % 1000) * 1000000L};
    nanosleep(&delay, NULL);
}

char *str_dup(const char *text) {
    return text ? strdup(text) : NULL;
}
#endif
