#include "progress.h"
#include <stdarg.h>
#include <stdio.h>
static const progress_reporter_t *reporter, *observer;
void progress_set(const progress_reporter_t *value) {
    reporter = value;
}
void progress_observe(const progress_reporter_t *value) {
    observer = value;
}
#define REPORT(method, args)                                                                                 \
    do {                                                                                                     \
        if (observer && observer->method)                                                                    \
            observer->method args;                                                                           \
        if (reporter && reporter->method)                                                                    \
            reporter->method args;                                                                           \
    } while (0)
void progress_stage(const char *format, ...) {
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    REPORT(stage, (text));
}
void progress_begin(int total) {
    REPORT(begin, (total));
}
void progress_file(int index, int total, const char *file) {
    REPORT(file, (index, total, file));
}
void progress_bytes(unsigned long long done, unsigned long long total) {
    REPORT(bytes, (done, total));
}
void progress_convert(const char *file) {
    REPORT(convert, (file));
}
void progress_end(int fresh, int skipped, int failed) {
    REPORT(end, (fresh, skipped, failed));
}
