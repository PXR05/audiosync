#ifndef _WIN32
#define _GNU_SOURCE
#include "platform/compat.h"
#include "platform/util.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <stdarg.h>
#include <unistd.h>

typedef struct {
    DIR *dir;
    char path[PATH_MAX];
} find_handle_t;
static __thread DWORD last_error;
static int format_has_length(const wchar_t *format, size_t percent, size_t conversion) {
    for (size_t i = percent + 1; i < conversion; ++i)
        if (format[i] == L'h' || format[i] == L'l' || format[i] == L'j' || format[i] == L'z' ||
            format[i] == L't' || format[i] == L'L')
            return 1;
    return 0;
}

int audiosync_snwprintf(wchar_t *out, size_t cap, const wchar_t *format, ...) {
    wchar_t portable[4096];
    size_t source = 0, dest = 0;
    while (format[source] && dest + 2 < sizeof portable / sizeof *portable) {
        if (format[source] != L'%') {
            portable[dest++] = format[source++];
            continue;
        }
        size_t percent = source;
        portable[dest++] = format[source++];
        if (format[source] == L'%') {
            portable[dest++] = format[source++];
            continue;
        }
        while (format[source] && !wcschr(L"diouxXfFeEgGaAcspn", format[source]))
            portable[dest++] = format[source++];
        if (format[source] == L's' && !format_has_length(format, percent, source))
            portable[dest++] = L'l';
        if (format[source])
            portable[dest++] = format[source++];
    }
    portable[dest] = 0;
    va_list args;
    va_start(args, format);
    int result = vswprintf(out, cap, portable, args);
    va_end(args);
    if (cap)
        out[cap - 1] = 0;
    return result;
}
static char *native_path(const wchar_t *path) {
    char *value = wide_to_utf8(path);
    if (value)
        for (char *p = value; *p; ++p)
            if (*p == '\\')
                *p = '/';
    return value;
}
DWORD GetLastError(void) {
    return last_error;
}
DWORD GetCurrentProcessId(void) {
    return (DWORD)getpid();
}
unsigned long long GetTickCount64(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000 + (unsigned long long)t.tv_nsec / 1000000;
}
DWORD GetTickCount(void) {
    return (DWORD)GetTickCount64();
}
DWORD GetFileAttributesW(const wchar_t *path) {
    char *native = native_path(path);
    struct stat info;
    if (!native || lstat(native, &info) != 0) {
        last_error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : (DWORD)errno;
        free(native);
        return INVALID_FILE_ATTRIBUTES;
    }
    free(native);
    DWORD result = S_ISDIR(info.st_mode) ? FILE_ATTRIBUTE_DIRECTORY : 0;
    if (S_ISLNK(info.st_mode))
        result |= FILE_ATTRIBUTE_REPARSE_POINT;
    return result;
}
DWORD GetFullPathNameW(const wchar_t *path, DWORD cap, wchar_t *out, wchar_t **leaf) {
    char *native = native_path(path), absolute[PATH_MAX];
    if (!native)
        return 0;
    if (native[0] == '/')
        snprintf(absolute, sizeof absolute, "%s", native);
    else {
        char cwd[PATH_MAX];
        if (!getcwd(cwd, sizeof cwd)) {
            free(native);
            return 0;
        }
        size_t cwd_length = strlen(cwd), native_length = strlen(native);
        if (cwd_length + native_length + 2 > sizeof absolute) {
            free(native);
            return 0;
        }
        memcpy(absolute, cwd, cwd_length);
        absolute[cwd_length] = '/';
        memcpy(absolute + cwd_length + 1, native, native_length + 1);
    }
    free(native);
    wchar_t *wide = utf8_to_wide(absolute);
    if (!wide)
        return 0;
    size_t length = wcslen(wide);
    if (length + 1 > cap) {
        free(wide);
        return (DWORD)(length + 1);
    }
    wcscpy(out, wide);
    if (leaf) {
        wchar_t *slash = wcsrchr(out, L'/');
        *leaf = slash ? slash + 1 : out;
    }
    free(wide);
    return (DWORD)length;
}
static int next_entry(find_handle_t *handle, WIN32_FIND_DATAW *data) {
    struct dirent *entry;
    while ((entry = readdir(handle->dir))) {
        wchar_t *name = utf8_to_wide(entry->d_name);
        if (!name)
            continue;
        wcsncpy(data->cFileName, name, MAX_PATH - 1);
        data->cFileName[MAX_PATH - 1] = 0;
        free(name);
        char full[PATH_MAX];
        size_t path_length = strlen(handle->path), name_length = strlen(entry->d_name);
        if (path_length + name_length + 2 > sizeof full)
            continue;
        memcpy(full, handle->path, path_length);
        full[path_length] = '/';
        memcpy(full + path_length + 1, entry->d_name, name_length + 1);
        struct stat info;
        data->dwFileAttributes = 0;
        if (lstat(full, &info) == 0) {
            if (S_ISDIR(info.st_mode))
                data->dwFileAttributes |= FILE_ATTRIBUTE_DIRECTORY;
            if (S_ISLNK(info.st_mode))
                data->dwFileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
        }
        return TRUE;
    }
    last_error = ERROR_NO_MORE_FILES;
    return FALSE;
}
HANDLE FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *data) {
    char *native = native_path(pattern);
    if (!native)
        return INVALID_HANDLE_VALUE;
    char *wildcard = strchr(native, '*');
    if (wildcard) {
        while (wildcard > native && wildcard[-1] == '.')
            --wildcard;
        *wildcard = 0;
    }
    size_t length = strlen(native);
    while (length > 1 && native[length - 1] == '/')
        native[--length] = 0;
    find_handle_t *handle = calloc(1, sizeof *handle);
    if (!handle) {
        free(native);
        return NULL;
    }
    snprintf(handle->path, sizeof handle->path, "%s", native);
    free(native);
    handle->dir = opendir(handle->path);
    if (!handle->dir) {
        last_error = errno == ENOENT ? ERROR_FILE_NOT_FOUND : (DWORD)errno;
        free(handle);
        return NULL;
    }
    if (!next_entry(handle, data)) {
        FindClose(handle);
        return NULL;
    }
    return handle;
}
int FindNextFileW(HANDLE handle, WIN32_FIND_DATAW *data) {
    return next_entry(handle, data);
}
void FindClose(HANDLE handle) {
    if (handle) {
        closedir(((find_handle_t *)handle)->dir);
        free(handle);
    }
}
int DeleteFileW(const wchar_t *path) {
    char *p = native_path(path);
    int ok = p && unlink(p) == 0;
    if (!ok)
        last_error = errno;
    free(p);
    return ok;
}
int RemoveDirectoryW(const wchar_t *path) {
    char *p = native_path(path);
    int ok = p && rmdir(p) == 0;
    if (!ok)
        last_error = errno;
    free(p);
    return ok;
}
int MoveFileExW(const wchar_t *source, const wchar_t *dest, DWORD flags) {
    char *from = native_path(source), *to = native_path(dest);
    int ok = 0;
    if (from && to) {
        if (!(flags & MOVEFILE_REPLACE_EXISTING) && access(to, F_OK) == 0)
            errno = EEXIST;
        else
            ok = rename(from, to) == 0;
    }
    if (!ok)
        last_error = errno;
    free(from);
    free(to);
    return ok;
}
int CopyFileExW(const wchar_t *source, const wchar_t *dest, LPPROGRESS_ROUTINE progress, void *data,
                int *cancel, DWORD flags) {
    (void)flags;
    char *from = native_path(source), *to = native_path(dest);
    if (!from || !to) {
        free(from);
        free(to);
        return FALSE;
    }
    int input = open(from, O_RDONLY), output = -1;
    struct stat info;
    if (input >= 0 && fstat(input, &info) == 0)
        output = open(to, O_WRONLY | O_CREAT | O_TRUNC, info.st_mode & 0777);
    long long done = 0;
    char buffer[131072];
    int ok = output >= 0;
    while (ok) {
        ssize_t n = read(input, buffer, sizeof buffer);
        if (n == 0)
            break;
        if (n < 0) {
            ok = 0;
            break;
        }
        for (ssize_t off = 0; off < n;) {
            ssize_t w = write(output, buffer + off, (size_t)(n - off));
            if (w <= 0) {
                ok = 0;
                break;
            }
            off += w;
        }
        done += n;
        if (progress) {
            LARGE_INTEGER total = {info.st_size}, copied = {done}, zero = {0};
            progress(total, copied, zero, zero, 0, 0, NULL, NULL, data);
        }
        if (cancel && *cancel)
            ok = 0;
    }
    if (input >= 0)
        close(input);
    if (output >= 0) {
        if (ok)
            fsync(output);
        close(output);
    }
    if (!ok)
        unlink(to);
    free(from);
    free(to);
    return ok;
}
int CopyFileW(const wchar_t *source, const wchar_t *dest, int fail_if_exists) {
    if (fail_if_exists && GetFileAttributesW(dest) != INVALID_FILE_ATTRIBUTES)
        return FALSE;
    return CopyFileExW(source, dest, NULL, NULL, NULL, 0);
}
DWORD GetTempPathA(DWORD cap, char *out) {
    const char *temp = getenv("TMPDIR");
    if (!temp || !*temp)
        temp = "/tmp";
    int n = snprintf(out, cap, "%s/", temp);
    return n > 0 && (DWORD)n < cap ? (DWORD)n : 0;
}
int MultiByteToWideChar(unsigned cp, DWORD flags, const char *source, int source_len, wchar_t *dest,
                        int dest_len) {
    (void)cp;
    (void)flags;
    (void)source_len;
    size_t n = mbstowcs(NULL, source, 0);
    if (n == (size_t)-1)
        return 0;
    if (!dest)
        return (int)n + 1;
    if (n + 1 > (size_t)dest_len)
        return 0;
    mbstowcs(dest, source, (size_t)dest_len);
    return (int)n + 1;
}
FILE *audiosync_wfopen(const wchar_t *path, const wchar_t *mode) {
    char *p = native_path(path), *m = wide_to_utf8(mode);
    FILE *f = p && m ? fopen(p, m) : NULL;
    free(p);
    free(m);
    return f;
}
#endif
