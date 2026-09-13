#ifndef CONVERT_H
#define CONVERT_H
#include <stddef.h>
#include <wchar.h>

void convert_normalize(const char *in, char *out, size_t cap);

int convert_needed(const char *fmt, const char *src_ext);

const char *convert_ext(const char *fmt);

int convert_find_ffmpeg(char *out, size_t cap);

int convert_run(const char *ffmpeg, const wchar_t *wsrc, const wchar_t *wdst, const char *fmt);

#endif
