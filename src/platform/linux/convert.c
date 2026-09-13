#ifndef _WIN32
#define _GNU_SOURCE
#include "convert.h"
#include "log.h"
#include "platform/util.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;

void convert_normalize(const char *input, char *out, size_t cap) {
    char value[32] = {0};
    snprintf(value, sizeof value, "%s", input ? input : "keep");
    for (char *p = value; *p; ++p)
        if (*p >= 'A' && *p <= 'Z')
            *p += 32;
    if (value[0] == '.')
        memmove(value, value + 1, strlen(value));
    const char *valid[] = {"mp3", "opus", "flac", "ogg", "oga", "m4a", "wav"};
    for (size_t i = 0; i < sizeof valid / sizeof *valid; ++i)
        if (!strcmp(value, valid[i])) {
            snprintf(out, cap, "%s", value);
            return;
        }
    snprintf(out, cap, "keep");
}

const char *convert_ext(const char *format) {
    if (!format || !strcmp(format, "keep"))
        return "";
    if (!strcmp(format, "oga"))
        return "ogg";
    return format;
}

int convert_needed(const char *format, const char *source_ext) {
    const char *wanted = convert_ext(format);
    if (!*wanted)
        return 0;
    if (!source_ext)
        return 1;
    while (*source_ext == '.')
        ++source_ext;
    if (!strcasecmp(wanted, source_ext))
        return 0;
    return strcmp(wanted, "ogg") || strcasecmp(source_ext, "oga");
}

int convert_find_ffmpeg(char *out, size_t cap) {
    const char *path = getenv("PATH");
    if (!path)
        return -1;
    char *copy = strdup(path), *state = NULL;
    for (char *dir = strtok_r(copy, ":", &state); dir; dir = strtok_r(NULL, ":", &state)) {
        char candidate[PATH_MAX];
        snprintf(candidate, sizeof candidate, "%s/ffmpeg", dir);
        if (access(candidate, X_OK) == 0) {
            snprintf(out, cap, "%s", candidate);
            free(copy);
            return 0;
        }
    }
    free(copy);
    return -1;
}

int convert_run(const char *ffmpeg, const wchar_t *wide_source, const wchar_t *wide_dest,
                const char *format) {
    char *source = wide_to_utf8(wide_source), *dest = wide_to_utf8(wide_dest);
    if (!source || !dest) {
        free(source);
        free(dest);
        return -1;
    }
    for (char *p = source; *p; ++p)
        if (*p == '\\')
            *p = '/';
    for (char *p = dest; *p; ++p)
        if (*p == '\\')
            *p = '/';
    static const struct {
        const char *format, *codec, *option, *value;
    } codecs[] = {
        {"mp3", "libmp3lame", "-q:a", "0"},
        {"opus", "libopus", "-b:a", "192k"},
        {"flac", "flac", "-compression_level", "8"},
        {"ogg", "libvorbis", "-q:a", "10"},
        {"m4a", "aac", "-q:a", "2"},
        {"wav", "pcm_s16le", NULL, NULL},
    };
    const char *ext = convert_ext(format);
    size_t codec = 0;
    while (codec < sizeof codecs / sizeof *codecs && strcmp(ext, codecs[codec].format))
        ++codec;
    if (codec == sizeof codecs / sizeof *codecs) {
        free(source);
        free(dest);
        return -1;
    }
    int wav = !strcmp(ext, "wav");
    char *argv[24] = {(char *)ffmpeg, "-y", "-nostdin", "-hide_banner", "-loglevel",
                      "warning",      "-i", source,     "-map",         wav ? "0:a" : "0"};
    int n = 10;
    if (!wav) {
        argv[n++] = "-map_metadata";
        argv[n++] = "0";
    }
    if (!strcmp(ext, "opus") || !strcmp(ext, "ogg")) {
        argv[n++] = "-map";
        argv[n++] = "-0:v";
    }
    argv[n++] = "-c:a";
    argv[n++] = (char *)codecs[codec].codec;
    if (codecs[codec].option) {
        argv[n++] = (char *)codecs[codec].option;
        argv[n++] = (char *)codecs[codec].value;
    }
    argv[n++] = dest;
    argv[n] = NULL;
    char log_path[PATH_MAX];
    snprintf(log_path, sizeof log_path, "%s/audiosync-ffmpeg.log",
             getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp");
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, log_path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    pid_t process;
    int error = posix_spawn(&process, ffmpeg, &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    int status = 1;
    if (!error)
        waitpid(process, &status, 0);
    int ok = !error && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (ok)
        unlink(log_path);
    else
        log_err("convert: ffmpeg failed%s", error ? strerror(error) : "");
    free(source);
    free(dest);
    return ok ? 0 : -1;
}
#endif
