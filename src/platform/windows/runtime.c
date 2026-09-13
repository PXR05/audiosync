#include "platform/runtime.h"
#include "config.h"
#include <stdlib.h>
#include <stdio.h>

#include <windows.h>
#include <shlobj.h>
static HANDLE locks[LOCK_COUNT], legacy_locks[LOCK_COUNT];
static const wchar_t *const legacy_names[LOCK_COUNT] = {L"Local\\AudioSyncSingleInstance",
                                                        L"Local\\AudioSyncSync", L"Local\\AudioSyncConfig"};
static void lock_name(int slot, wchar_t *name, size_t cap) {
    char *path = config_path();
    unsigned long long hash = 14695981039346656037ULL;
    if (path) {
        for (const unsigned char *p = (const unsigned char *)path; *p; ++p) {
            unsigned char c = *p == '\\' ? '/' : *p;
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
            hash = (hash ^ c) * 1099511628211ULL;
        }
        free(path);
    }
    _snwprintf(name, cap, L"Local\\AudioSync-%016llx-%d", hash, slot);
}
static int default_config(void) {
    wchar_t actual[MAX_PATH], selected[MAX_PATH];
    if (SHGetFolderPathW(NULL, CSIDL_APPDATA, NULL, SHGFP_TYPE_CURRENT, actual) != S_OK)
        return 0;
    DWORD n = GetEnvironmentVariableW(L"APPDATA", selected, MAX_PATH);
    return n && n < MAX_PATH && !_wcsicmp(actual, selected);
}
static int acquire(HANDLE handle) {
    if (!handle)
        return 0;
    DWORD result = WaitForSingleObject(handle, 0);
    return result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
}
int runtime_lock(int slot) {
    wchar_t name[80];
    lock_name(slot, name, 80);
    HANDLE lock = CreateMutexW(NULL, FALSE, name);
    if (!acquire(lock)) {
        if (lock)
            CloseHandle(lock);
        return 0;
    }
    /* Preserve installer detection and serialization with the previous release. */
    if (default_config()) {
        HANDLE legacy = CreateMutexW(NULL, FALSE, legacy_names[slot]);
        if (!acquire(legacy)) {
            if (legacy)
                CloseHandle(legacy);
            ReleaseMutex(lock);
            CloseHandle(lock);
            return 0;
        }
        legacy_locks[slot] = legacy;
    }
    locks[slot] = lock;
    return 1;
}
void runtime_unlock(int slot) {
    if (legacy_locks[slot]) {
        ReleaseMutex(legacy_locks[slot]);
        CloseHandle(legacy_locks[slot]);
        legacy_locks[slot] = NULL;
    }
    ReleaseMutex(locks[slot]);
    CloseHandle(locks[slot]);
    locks[slot] = NULL;
}
static int busy_name(const wchar_t *name) {
    HANDLE lock = OpenMutexW(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, name);
    if (!lock)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    DWORD result = WaitForSingleObject(lock, 0);
    if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
        ReleaseMutex(lock);
    CloseHandle(lock);
    return result == WAIT_TIMEOUT ? 1 : result == WAIT_FAILED ? -1 : 0;
}
int runtime_busy(int slot) {
    wchar_t name[80];
    lock_name(slot, name, 80);
    int result = busy_name(name);
    if (result == 0 && default_config()) {
        result = busy_name(legacy_names[slot]);
    }
    return result;
}
