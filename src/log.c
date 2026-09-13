#include "log.h"
#include "platform/files.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static int quiet;
void log_set_quiet(int enabled) {
    quiet = enabled;
}
static void write_file(const char *line) {
    char *path = state_path("audiosync.log", 1);
    if (!path)
        return;
    FILE *file = file_open_utf8(path, "rb");
    long size = 0;
    if (file) {
        fseek(file, 0, SEEK_END);
        size = ftell(file);
        fclose(file);
    }
    if (size > 512 * 1024) {
        char *old = state_path("audiosync.log.old", 0);
        if (old)
            file_replace_utf8(path, old);
        free(old);
    }
    file = file_open_utf8(path, "ab");
    if (file) {
        fprintf(file, "%s\n", line);
        fclose(file);
    }
    free(path);
}
static void vlog(const char *level, const char *format, va_list args) {
    time_t now = time(NULL);
    struct tm local;
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char line[2048];
    size_t offset = strftime(line, sizeof line, "[%Y-%m-%d %H:%M:%S]", &local);
    int n = snprintf(line + offset, sizeof line - offset, "[%s] ", level);
    if (n > 0)
        offset += (size_t)n;
    if (offset < sizeof line)
        vsnprintf(line + offset, sizeof line - offset, format, args);
    if (!quiet || strcmp(level, "info"))
        fprintf(stderr, "%s\n", line);
    write_file(line);
}
void log_info(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vlog("info", format, args);
    va_end(args);
}
void log_warn(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vlog("warn", format, args);
    va_end(args);
}
void log_err(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vlog("error", format, args);
    va_end(args);
}
