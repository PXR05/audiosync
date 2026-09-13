#ifndef AUDIOSYNC_COMPAT_H
#define AUDIOSYNC_COMPAT_H
#ifdef _WIN32
#include <windows.h>
#else
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <wchar.h>
#define MAX_PATH 4096
#define CALLBACK
#define FALSE 0
#define TRUE 1
#define INVALID_FILE_ATTRIBUTES ((unsigned long)-1)
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_REPARSE_POINT 0x400
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_NO_MORE_FILES 18
#define MOVEFILE_REPLACE_EXISTING 1
#define MOVEFILE_WRITE_THROUGH 8
#define PROGRESS_CONTINUE 0
#define CP_UTF8 65001
typedef unsigned long DWORD;
typedef void *HANDLE;
typedef void *LPVOID;
typedef struct {
    long long QuadPart;
} LARGE_INTEGER;
typedef struct {
    DWORD dwFileAttributes;
    wchar_t cFileName[MAX_PATH];
} WIN32_FIND_DATAW;
#define INVALID_HANDLE_VALUE NULL
#define _stricmp strcasecmp
#define _wcsnicmp wcsncasecmp
#define _wcsdup wcsdup
#define _strdup strdup
int audiosync_snwprintf(wchar_t *out, size_t cap, const wchar_t *format, ...);
#define _snwprintf audiosync_snwprintf
typedef DWORD (*LPPROGRESS_ROUTINE)(LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, LARGE_INTEGER, DWORD, DWORD,
                                    HANDLE, HANDLE, LPVOID);
DWORD GetLastError(void);
DWORD GetTickCount(void);
unsigned long long GetTickCount64(void);
DWORD GetCurrentProcessId(void);
DWORD GetFileAttributesW(const wchar_t *path);
DWORD GetFullPathNameW(const wchar_t *path, DWORD cap, wchar_t *out, wchar_t **leaf);
HANDLE FindFirstFileW(const wchar_t *pattern, WIN32_FIND_DATAW *data);
int FindNextFileW(HANDLE handle, WIN32_FIND_DATAW *data);
void FindClose(HANDLE handle);
int DeleteFileW(const wchar_t *path);
int RemoveDirectoryW(const wchar_t *path);
int MoveFileExW(const wchar_t *source, const wchar_t *dest, DWORD flags);
int CopyFileW(const wchar_t *source, const wchar_t *dest, int fail_if_exists);
int CopyFileExW(const wchar_t *source, const wchar_t *dest, LPPROGRESS_ROUTINE progress, void *data,
                int *cancel, DWORD flags);
DWORD GetTempPathA(DWORD cap, char *out);
int MultiByteToWideChar(unsigned codepage, DWORD flags, const char *source, int source_len, wchar_t *dest,
                        int dest_len);
FILE *audiosync_wfopen(const wchar_t *path, const wchar_t *mode);
#define _wfopen audiosync_wfopen
#endif
#endif
