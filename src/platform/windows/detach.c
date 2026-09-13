#include "platform/detach.h"
#include <conio.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <windows.h>

#define CHILD_ENV L"AUDIOSYNC_DETACHED_CHILD"
static volatile LONG interrupted;

int detach_is_child(void) {
    return GetEnvironmentVariableW(CHILD_ENV, NULL, 0) > 0;
}

static DWORD WINAPI silence(void *data) {
    HANDLE event = data;
    WaitForSingleObject(event, INFINITE);
    freopen("NUL", "r", stdin);
    freopen("NUL", "w", stdout);
    freopen("NUL", "w", stderr);
    FreeConsole();
    CloseHandle(event);
    return 0;
}

void detach_prepare(void) {
    wchar_t value[32];
    if (GetEnvironmentVariableW(CHILD_ENV, value, 32) > 0) {
        HANDLE event = (HANDLE)(uintptr_t)_wcstoui64(value, NULL, 10);
        HANDLE thread = CreateThread(NULL, 0, silence, event, 0, NULL);
        if (thread)
            CloseHandle(thread);
    }
}

static BOOL WINAPI interrupt(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&interrupted, 1);
        return TRUE;
    }
    return FALSE;
}

int detach_run(int argc, char **argv, int immediate) {
    (void)argc;
    (void)argv;
    wchar_t value[32], *command = _wcsdup(GetCommandLineW());
    SECURITY_ATTRIBUTES security = {sizeof security, NULL, TRUE};
    HANDLE event = CreateEventW(&security, TRUE, FALSE, NULL);
    if (!event || !command) {
        CloseHandle(event);
        free(command);
        return -1;
    }
    _snwprintf(value, 32, L"%llu", (unsigned long long)(uintptr_t)event);
    SetEnvironmentVariableW(CHILD_ENV, value);
    STARTUPINFOW startup = {0};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION process = {0};
    int created = CreateProcessW(NULL, command, NULL, NULL, TRUE, CREATE_NEW_PROCESS_GROUP, NULL, NULL,
                                 &startup, &process);
    SetEnvironmentVariableW(CHILD_ENV, NULL);
    free(command);
    if (!created) {
        CloseHandle(event);
        return -1;
    }
    CloseHandle(process.hThread);
    if (immediate) {
        SetEvent(event);
        CloseHandle(event);
        CloseHandle(process.hProcess);
        return 0;
    }

    SetConsoleCtrlHandler(interrupt, TRUE);
    fputs("Press d to detach.\n", stderr);
    int detached = 0;
    while (WaitForSingleObject(process.hProcess, 50) == WAIT_TIMEOUT) {
        if (_kbhit()) {
            int key = _getch();
            if (key == 'd' || key == 'D') {
                SetEvent(event);
                detached = 1;
                break;
            }
        }
        if (InterlockedCompareExchange(&interrupted, 0, 0)) {
            GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, process.dwProcessId);
            WaitForSingleObject(process.hProcess, INFINITE);
            break;
        }
    }
    SetConsoleCtrlHandler(interrupt, FALSE);
    DWORD code = 1;
    if (!detached)
        GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(event);
    CloseHandle(process.hProcess);
    if (detached)
        fputs("Detached. Use 'audiosync status -w' or 'audiosync logs -f' to reconnect.\n", stderr);
    return detached ? 0 : (int)code;
}
