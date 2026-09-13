#include "platform/files.h"
#include "platform/util.h"
#include "config.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#include <stdint.h>
#else
#include <sys/stat.h>
#endif
char *state_path(const char *name, int create_directory) {
    char *config = config_path();
    if (!config)
        return NULL;
    char *slash = strrchr(config, '/');
#ifdef _WIN32
    char *backslash = strrchr(config, '\\');
    if (!slash || (backslash && backslash > slash))
        slash = backslash;
#endif
    if (!slash) {
        free(config);
        return NULL;
    }
    *slash = 0;
    if (create_directory) {
        wchar_t *dir = utf8_to_wide(config);
        int result = dir ? mkdirs_w(dir) : -1;
        free(dir);
        if (result) {
            free(config);
            return NULL;
        }
    }
    size_t length = strlen(config) + strlen(name) + 2;
    char *path = malloc(length);
    if (path)
        snprintf(path, length, "%s/%s", config, name);
    free(config);
    return path;
}
FILE *file_open_utf8(const char *path, const char *mode) {
    if (!path)
        return NULL;
    wchar_t *wide = utf8_to_wide(path), *wmode = utf8_to_wide(mode);
    FILE *file = NULL;
#ifdef _WIN32
    if (wide && !strcmp(mode, "rb")) {
        /* Readers must allow atomic snapshot replacement while status is followed. */
        HANDLE handle =
            CreateFileW(wide, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (handle != INVALID_HANDLE_VALUE) {
            int fd = _open_osfhandle((intptr_t)handle, _O_RDONLY | _O_BINARY);
            if (fd >= 0) {
                file = _fdopen(fd, "rb");
                if (!file)
                    _close(fd);
            } else
                CloseHandle(handle);
        } else {
            DWORD error = GetLastError();
            errno = error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT : EACCES;
        }
    } else
#endif
        if (wide && wmode)
        file = _wfopen(wide, wmode);

    int open_error = errno;
    free(wide);
    free(wmode);
#ifndef _WIN32
    if (file && (strchr(mode, 'w') || strchr(mode, 'a')))
        fchmod(fileno(file), 0600);
#endif
    if (!file)
        errno = open_error;
    return file;
}
int file_replace_utf8(const char *from, const char *to) {
    wchar_t *a = utf8_to_wide(from), *b = utf8_to_wide(to);
    int result = a && b && MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
    free(a);
    free(b);
    return result;
}
