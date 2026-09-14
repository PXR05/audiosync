#ifndef _WIN32
#define _GNU_SOURCE
#include <sys/stat.h>
#endif
#include <assert.h>
#define convert_find_ffmpeg test_find_ffmpeg
#define convert_run test_convert_run
#include "sync.c"
#undef convert_find_ffmpeg
#undef convert_run
static int fresh, skipped, failed, downloads, conversions, fail_download, fail_convert;
static void finish(int a, int b, int c) {
    fresh = a;
    skipped = b;
    failed = c;
}
static void put(const wchar_t *path, const char *s) {
    ensure_parent_w(path);
    FILE *f = _wfopen(path, L"wb");
    assert(f);
    fputs(s, f);
    fclose(f);
}
static void check(const wchar_t *path, const char *expected) {
    char b[100] = {0};
    FILE *f = _wfopen(path, L"rb");
    assert(f);
    assert(fread(b, 1, 99, f) > 0);
    fclose(f);
    assert(!strcmp(b, expected));
}
int test_find_ffmpeg(char *out, size_t cap) {
    snprintf(out, cap, "mock");
    return 0;
}
int test_convert_run(const char *exe, const wchar_t *src, const wchar_t *dst, const char *fmt) {
    (void)exe;
    (void)src;
    (void)fmt;
    conversions++;
    put(dst, "converted");
    return fail_convert ? -1 : 0;
}
static int adapter_connect(const remote_source_t *source, void **session) {
    (void)source;
    *session = (void *)1;
    return 0;
}
static int adapter_playlists(void *session, remote_playlist_t **out, int *n) {
    (void)session;
    *out = NULL;
    *n = 0;
    return 0;
}
static int adapter_tracks(void *session, remote_track_t **out, int *n) {
    (void)session;
    *n = 1;
    *out = calloc(1, sizeof **out);
    strcpy((*out)->id, "regression-track");
    strcpy((*out)->filename, "song.flac");
    strcpy((*out)->title, "Song");
    strcpy((*out)->artist, "Artist");
    strcpy((*out)->album, "Album");
    /* Remote sizes are progress estimates, not an integrity contract. */
    (*out)->size = 999;
    return 0;
}
static int adapter_download(void *session, const char *id, const wchar_t *p, remote_progress_cb cb,
                            void *ctx) {
    (void)session;
    (void)id;
    (void)cb;
    (void)ctx;
    downloads++;
    put(p, fail_download == 2 ? "" : "remote");
    return fail_download == 1 ? -1 : 0;
}
static void adapter_disconnect(void *session) {
    (void)session;
}
static const remote_adapter_t test_adapter = {
    .name = "test",
    .connect = adapter_connect,
    .list_tracks = adapter_tracks,
    .list_playlists = adapter_playlists,
    .download = adapter_download,
    .disconnect = adapter_disconnect,
};

int main(void) {
    wchar_t root[512], src[512], dst[512], path[1024];
    _snwprintf(root, 512, L"build\\regression-%lu", GetCurrentProcessId());
    mkdirs_w(root);
    _snwprintf(src, 512, L"%s\\source", root);
    _snwprintf(dst, 512, L"%s\\target", root);
    mkdirs_w(src);
    mkdirs_w(dst);
    char *source = wide_to_utf8(src);
    progress_reporter_t r = {0};
    r.end = finish;
    progress_set(&r);
    _snwprintf(path, 1024, L"%s\\song.flac", src);
    put(path, "new source bytes");
    _snwprintf(path, 1024, L"%s\\song.flac", dst);
    put(path, "existing");
    assert(sync_local_to(source, dst, 0, "keep") == 0);
    assert(fresh == 0 && skipped == 1 && failed == 0);
    check(path, "existing");
    DeleteFileW(path);
    assert(sync_local_to(source, dst, 0, "keep") == 0);
    assert(fresh == 1 && skipped == 0);
    check(path, "new source bytes");
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA before, after;
    GetFileAttributesExW(path, GetFileExInfoStandard, &before);
#else
    char *timestamp_path = wide_to_utf8(path);
    for (char *p = timestamp_path; *p; ++p)
        if (*p == 92)
            *p = '/';
    struct stat before, after;
    assert(stat(timestamp_path, &before) == 0);
#endif
    assert(sync_local_to(source, dst, 0, "keep") == 0);
    assert(skipped == 1);
#ifdef _WIN32
    GetFileAttributesExW(path, GetFileExInfoStandard, &after);
    assert(CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) == 0);
#else
    assert(stat(timestamp_path, &after) == 0);
    assert(before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec);
    free(timestamp_path);
#endif
    _snwprintf(path, 1024, L"%s\\song.mp3", dst);
    put(path, "user conversion");
    assert(sync_local_to(source, dst, 0, "mp3") == 0);
    assert(skipped == 1 && conversions == 0);
    check(path, "user conversion");
    DeleteFileW(path);
    assert(sync_local_to(source, dst, 0, "mp3") == 0);
    assert(fresh == 1 && conversions == 1);
    check(path, "converted");
    _snwprintf(path, 1024, L"%s\\precious.txt", dst);
    put(path, "keep me");
    assert(sync_local_to("nonexistent-source-folder", dst, 1, "keep") == -1);
    check(path, "keep me");
    assert(sync_local_to(source, src, 1, "keep") == -1);
    wchar_t nested[1024];
    _snwprintf(nested, 1024, L"%s\\nested", src);
    assert(sync_local_to(source, nested, 1, "keep") == -1);
    _snwprintf(path, 1024, L"%s\\song.mp3", dst);
    DeleteFileW(path);
    fail_convert = 1;
    assert(sync_local_to(source, dst, 1, "mp3") == -1);
    assert(failed == 1);
    assert(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
    _snwprintf(path, 1024, L"%s\\precious.txt", dst);
    check(path, "keep me");
    fail_convert = 0;
    assert(sync_local_to(source, dst, 1, "keep") == 0);
    assert(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
    assert(sync_target_valid("Music"));
    assert(sync_target_valid("Audio/Albums"));
    assert(!sync_target_valid(""));
    assert(!sync_target_valid("../Music"));
    assert(!sync_target_valid("C:\\Music"));
    assert(!sync_target_valid("\\Music"));
    assert(!sync_target_valid("Music/.."));

    wchar_t absolute[1024];
    GetFullPathNameW(root, 1024, absolute, NULL);
#ifdef _WIN32
    SetEnvironmentVariableW(L"TEMP", absolute);
#else
    char *temp_dir = wide_to_utf8(absolute);
    setenv("TMPDIR", temp_dir, 1);
    free(temp_dir);
#endif
    device_cfg_t d = {0};
    strcpy(d.format, "keep");
    d.layout = CFG_LAYOUT_D;
    _snwprintf(path, 1024, L"%s\\Album\\01. Artist - Song.flac", dst);
    put(path, "already here");
    assert(sync_remote_to(&d, dst, &test_adapter) == 0);
    assert(skipped == 1 && downloads == 0);
    check(path, "already here");
    DeleteFileW(path);
    assert(sync_remote_to(&d, dst, &test_adapter) == 0);
    assert(fresh == 1 && downloads == 1);
    check(path, "remote");
    assert(sync_remote_to(&d, dst, &test_adapter) == 0);
    assert(skipped == 1 && downloads == 1);
    strcpy(d.format, "mp3");
    _snwprintf(path, 1024, L"%s\\Album\\01. Artist - Song.mp3", dst);
    put(path, "existing mp3");
    int previous = conversions;
    assert(sync_remote_to(&d, dst, &test_adapter) == 0);
    assert(skipped == 1 && conversions == previous);
    check(path, "existing mp3");

    strcpy(d.format, "keep");
    d.mirror = 1;
    _snwprintf(path, 1024, L"%s\\Album\\01. Artist - Song.flac", dst);
    DeleteFileW(path);
    fail_download = 2;
    assert(sync_remote_to(&d, dst, &test_adapter) == -1);
    assert(failed == 1);
    assert(GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES);
    _snwprintf(path, 1024, L"%s\\Album\\01. Artist - Song.mp3", dst);
    check(path, "existing mp3");
    fail_download = 0;
    d.mirror = 0;
    d.layout = CFG_LAYOUT_A;
    assert(sync_remote_to(&d, dst, &test_adapter) == 0);
    assert(fresh == 1);
    _snwprintf(path, 1024, L"%s\\Artist\\Album\\Song.flac", dst);
    check(path, "remote");
    drive_info_t drive = {0};
    strcpy(d.serial, "WRONG");
    strcpy(d.label, "SAME");
    strcpy(drive.serial, "RIGHT");
    strcpy(drive.label, "SAME");
    assert(sync_device(&d, &drive, 1) == 1);
    free(source);
    puts("PASS: local/remote skip, repeat timestamps, atomic conversion failure, mirror safety, path "
         "validation, serial matching");
    return 0;
}
