#include "cli/internal.h"
#include "progress.h"
#include "platform/util.h"
#include <stdio.h>
#include <string.h>
static int terminal, line_open;
static char prefix[256];
static unsigned long long last_update;
static void newline(void) {
    if (line_open)
        fputc('\n', stderr);
    line_open = 0;
}
static void stage(const char *message) {
    newline();
    fprintf(stderr, "%s\n", message);
    fflush(stderr);
}
static void begin(int total) {
    terminal = cli_is_terminal(stderr);
    newline();
    if (total > 0)
        fprintf(stderr, "Syncing %d files\n", total);
    else
        fputs("Syncing files\n", stderr);
}
static void file(int index, int total, const char *path) {
    newline();
    if (!terminal)
        fprintf(stderr, "[%d/%d] %s\n", index, total, path);
    else {
        snprintf(prefix, sizeof prefix, "[%d/%d] %.180s", index, total, path);
        fprintf(stderr, "%s", prefix);
        line_open = 1;
    }
    last_update = 0;
    fflush(stderr);
}
static void bytes(unsigned long long done, unsigned long long total) {
    if (!terminal)
        return;
    unsigned long long now = GetTickCount64();
    if (now - last_update < 100 && done != total)
        return;
    last_update = now;
    if (total)
        fprintf(stderr, "\r%s  %3.0f%%    ", prefix, (double)done / total * 100.0);
    else
        fprintf(stderr, "\r%s  %.1f MiB    ", prefix, (double)done / 1048576.0);
    line_open = 1;
    fflush(stderr);
}
static void convert(const char *path) {
    if (terminal) {
        fprintf(stderr, "\r%s  converting...    ", prefix);
        line_open = 1;
    } else
        fprintf(stderr, "Converting %s\n", path);
    fflush(stderr);
}
static void end(int fresh, int skipped, int failed) {
    newline();
    fprintf(stderr, "Done: %d new/updated, %d up-to-date, %d failed\n", fresh, skipped, failed);
    fflush(stderr);
}
const progress_reporter_t progress_cli = {stage, begin, file, bytes, convert, end};
