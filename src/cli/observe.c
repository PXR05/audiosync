#include "cli/internal.h"
#include "status.h"
#include "platform/runtime.h"
#include "platform/files.h"
#include "json.h"
#include "platform/util.h"
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#endif
static volatile sig_atomic_t interrupted;
static void interrupt(int value) {
    (void)value;
    interrupted = 1;
}
static int status_command(int follow, int json) {
    sync_status_t previous = {0};
    int first = 1, old_watcher = -2, old_sync = -2, old_known = -1;
    if (follow && !json)
        fputs("Watching sync status. Press Ctrl+C to stop.\n", stderr);
    do {
        sync_status_t status;
        int known = status_read(&status);
        int watcher = runtime_busy(LOCK_WATCH), syncing = runtime_busy(LOCK_SYNC);
        if (watcher < 0 || syncing < 0) {
            fputs("error: cannot inspect runtime locks\n", stderr);
            return 1;
        }
        if (first || known != old_known || watcher != old_watcher || syncing != old_sync ||
            memcmp(&previous, &status, sizeof status)) {
            if (json) {
                printf("{\"watcher_running\":%s,\"sync_running\":%s,\"progress_available\":%s,\"last_sync\":",
                       watcher ? "true" : "false", syncing ? "true" : "false", known ? "true" : "false");
                if (!known)
                    fputs("null", stdout);
                else {
                    printf("{\"pid\":%lld,\"updated\":%lld,\"result\":", status.pid, status.updated);
                    if (status.result < 0)
                        fputs("null", stdout);
                    else
                        printf("%d", status.result);
                    printf(",\"interrupted\":%s,\"file_index\":%d,\"file_total\":%d,\"bytes\":%llu,"
                           "\"bytes_total\":%llu,\"new\":%d,\"skipped\":%d,\"failed\":%d,\"stage\":",
                           !syncing && status.result < 0 ? "true" : "false", status.index, status.total,
                           status.bytes, status.bytes_total, status.fresh, status.skipped, status.failed);
                    json_write_string(stdout, status.stage);
                    fputs(",\"file\":", stdout);
                    json_write_string(stdout, status.file);
                    putchar('}');
                }
                puts("}");
            } else {
                printf("Watcher: %s | Sync: %s\n", watcher ? "running" : "stopped",
                       syncing ? "running" : "idle");
                if (!known)
                    puts("No sync progress recorded yet. Start a sync or the tray watcher.");
                else {
                    printf("  %s\n",
                           !syncing && status.result < 0 ? "Last sync was interrupted" : status.stage);
                    if (status.file[0]) {
                        printf("  [%d/%d] %s", status.index, status.total, status.file);
                        if (status.bytes_total)
                            printf(" (%.0f%% of file)", (double)status.bytes / status.bytes_total * 100.0);
                        putchar('\n');
                    }
                    printf("  %d new, %d skipped, %d failed\n", status.fresh, status.skipped, status.failed);
                }
            }
            fflush(stdout);
            previous = status;
            old_known = known;
            old_watcher = watcher;
            old_sync = syncing;
            first = 0;
        }
        if (follow)
            sleep_ms(500);
    } while (follow && !interrupted);
    return interrupted ? 130 : 0;
}
static long file_length(FILE *file) {
    if (fseek(file, 0, SEEK_END))
        return -1;
    return ftell(file);
}
static long tail_start(FILE *file, long size, int lines) {
    if (!lines)
        return size;
    int newlines = 0;
    for (long offset = size; offset > 0;) {
        unsigned char block[4096];
        long start = offset > (long)sizeof block ? offset - (long)sizeof block : 0;
        fseek(file, start, SEEK_SET);
        size_t length = fread(block, 1, (size_t)(offset - start), file);
        for (size_t i = length; i > 0; --i) {
            long position = start + (long)i - 1;
            if (block[i - 1] == '\n' && position != size - 1 && ++newlines == lines)
                return position + 1;
        }
        offset = start;
    }
    return 0;
}
static int logs_command(int follow, int lines, int path_only) {
    char *path = state_path("audiosync.log", 0);
    if (!path) {
        fputs("error: cannot locate log directory\n", stderr);
        return 1;
    }
    if (path_only) {
        puts(path);
        free(path);
        return 0;
    }
    if (follow)
        fputs("Following activity log. Press Ctrl+C to stop.\n", stderr);
    long offset = 0;
    int first = 1;
    char identity[128] = {0};
    size_t identity_size = 0;
    do {
        errno = 0;
        FILE *file = file_open_utf8(path, "rb");
        if (file) {
            long size = file_length(file);
            if (size < 0) {
                fclose(file);
                free(path);
                return 1;
            }
            char prefix[128];
            rewind(file);
            size_t length = fread(prefix, 1, sizeof prefix, file);
            if (first)
                offset = tail_start(file, size, lines);
            else if (size < offset || length < identity_size || memcmp(identity, prefix, identity_size))
                offset = 0;
            memcpy(identity, prefix, length);
            identity_size = length;
            fseek(file, offset, SEEK_SET);
            char block[4096];
            size_t n;
            while ((n = fread(block, 1, sizeof block, file)) > 0)
                fwrite(block, 1, n, stdout);
            offset = ftell(file);
            int failed = ferror(file);
            fclose(file);
            if (failed) {
                free(path);
                return 1;
            }
            fflush(stdout);
            first = 0;
        } else if (errno != ENOENT) {
            fputs("error: cannot read activity log\n", stderr);
            free(path);
            return 1;
        } else if (first) {
            fputs("No activity log yet. Run a sync to create it.\n", stderr);
            first = 0;
        }
        if (follow)
            sleep_ms(250);
    } while (follow && !interrupted);
    free(path);
    return interrupted ? 130 : 0;
}
int cli_observe(int argc, char **argv, int json) {
    int status = !strcmp(argv[1], "status"), follow = 0, lines = 30, path = 0;
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (!strcmp(arg, status ? "--watch" : "--follow"))
            follow = 1;
        else if (!status && !strcmp(arg, "--path"))
            path = 1;
        else if (!status && !strcmp(arg, "--lines") && i + 1 < argc) {
            char *end = NULL;
            errno = 0;
            long value = strtol(argv[++i], &end, 10);
            if (errno || !*argv[i] || *end || value < 0 || value > 10000)
                return cli_error(argv[1], "--lines must be an integer from 0 to 10000");
            lines = (int)value;
        } else
            return cli_error(argv[1], "unknown option or extra argument '%s'", arg);
    }
    if (path && follow)
        return cli_error("logs", "--path cannot be combined with --follow");
    interrupted = 0;
    void (*old_int)(int) = signal(SIGINT, interrupt);
    void (*old_term)(int) = signal(SIGTERM, interrupt);
#ifdef _WIN32
    int old_mode = !status ? _setmode(_fileno(stdout), _O_BINARY) : -1;
#endif
    int result = status ? status_command(follow, json) : logs_command(follow, lines, path);
#ifdef _WIN32
    if (old_mode != -1)
        _setmode(_fileno(stdout), old_mode);
#endif
    signal(SIGINT, old_int);
    signal(SIGTERM, old_term);
    return result;
}
