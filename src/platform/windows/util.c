#include "platform/util.h"
#include <stdlib.h>
#include <string.h>

wchar_t *utf8_to_wide(const char *s) {
    if (!s)
        return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0)
        return NULL;
    wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (!w)
        return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

char *wide_to_utf8(const wchar_t *ws) {
    if (!ws)
        return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL);
    if (n <= 0)
        return NULL;
    char *s = (char *)malloc((size_t)n);
    if (!s)
        return NULL;
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s, n, NULL, NULL);
    return s;
}

int mkdirs_w(const wchar_t *path) {
    wchar_t tmp[MAX_PATH * 2];
    wcsncpy(tmp, path, sizeof(tmp) / sizeof(tmp[0]) - 1);
    tmp[sizeof(tmp) / sizeof(tmp[0]) - 1] = 0;
    size_t len = wcslen(tmp);
    if (len == 0)
        return -1;
    if (tmp[len - 1] == L'\\')
        tmp[len - 1] = 0;
    for (wchar_t *p = tmp + 1; *p; p++) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t save = *p;
            *p = 0;
            CreateDirectoryW(tmp, NULL);
            *p = save;
        }
    }
    if (!CreateDirectoryW(tmp, NULL) && GetLastError() != ERROR_ALREADY_EXISTS) {

        DWORD a = GetFileAttributesW(tmp);
        if (a == INVALID_FILE_ATTRIBUTES)
            return -1;
    }
    return 0;
}

int file_size_w(const wchar_t *path, unsigned long long *out) {
    WIN32_FILE_ATTRIBUTE_DATA d;
    if (!GetFileAttributesExW(path, GetFileExInfoStandard, &d))
        return -1;
    if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        return -1;
    *out = ((unsigned long long)d.nFileSizeHigh << 32) | d.nFileSizeLow;
    return 0;
}

void sleep_ms(int ms) {
    Sleep((DWORD)(ms < 0 ? 0 : ms));
}

char *str_dup(const char *s) {
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d)
        memcpy(d, s, n);
    return d;
}
