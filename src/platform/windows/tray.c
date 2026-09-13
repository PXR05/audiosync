#include "platform/tray.h"
#include "platform/runtime.h"
#include "log.h"
#include <windows.h>
#include <shellapi.h>
#include <stdio.h>
#include <string.h>

#define WM_TRAYICON (WM_APP + 1)
#define WM_SYNC_DONE (WM_APP + 2)
#define ID_SYNC 11
#define ID_PAUSE 12
#define ID_AUTOST 13
#define ID_EXIT 14
static HWND window;
static HANDLE worker;
static int paused, pending, stopping, with_tray;
static DWORD last_mask;
static NOTIFYICONDATAW icon;

static void tip(const wchar_t *text) {
    wcsncpy(icon.szTip, text, 127);
    icon.szTip[127] = 0;
    if (with_tray)
        Shell_NotifyIconW(NIM_MODIFY, &icon);
}
static DWORD WINAPI sync_worker(void *data) {
    (void)data;
    int result = runtime_sync(NULL);
    PostMessageW(window, WM_SYNC_DONE, (WPARAM)result, 0);
    return 0;
}
static void start_sync(void) {
    if (stopping)
        return;
    if (worker) {
        pending = 1;
        return;
    }
    tip(L"AudioSync - syncing");
    worker = CreateThread(NULL, 0, sync_worker, NULL, 0, NULL);
    if (!worker) {
        log_err("Could not start sync worker");
        tip(L"AudioSync - sync failed");
    }
}
static BOOL WINAPI control(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        PostMessageW(window, WM_CLOSE, 0, 0);
        return TRUE;
    }
    return FALSE;
}
static LRESULT CALLBACK wndproc(HWND h, UINT message, WPARAM w, LPARAM l) {
    static UINT taskbar_created;
    if (!taskbar_created)
        taskbar_created = RegisterWindowMessageW(L"TaskbarCreated");
    if (message == taskbar_created && with_tray) {
        Shell_NotifyIconW(NIM_ADD, &icon);
        return 0;
    }
    if (message == WM_TRAYICON && (l == WM_RBUTTONUP || l == WM_LBUTTONUP)) {
        POINT point;
        GetCursorPos(&point);
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | (worker ? MF_GRAYED : 0), ID_SYNC, L"Sync now");
        AppendMenuW(menu, MF_STRING | (paused ? MF_CHECKED : 0), ID_PAUSE,
                    paused ? L"Resume auto-sync" : L"Pause auto-sync");
        AppendMenuW(menu, MF_STRING | (autostart_is_enabled() ? MF_CHECKED : 0), ID_AUTOST,
                    L"Start at login");
        AppendMenuW(menu, MF_SEPARATOR, 0, NULL);
        AppendMenuW(menu, MF_STRING, ID_EXIT, L"Exit");
        SetForegroundWindow(h);
        TrackPopupMenu(menu, TPM_BOTTOMALIGN | TPM_LEFTALIGN, point.x, point.y, 0, h, NULL);
        PostMessageW(h, WM_NULL, 0, 0);
        DestroyMenu(menu);
        return 0;
    }
    if (message == WM_COMMAND) {
        switch (LOWORD(w)) {
        case ID_SYNC:
            start_sync();
            break;
        case ID_PAUSE:
            paused = !paused;
            tip(paused ? L"AudioSync - paused" : L"AudioSync");
            if (!paused)
                start_sync();
            break;
        case ID_AUTOST:
            if (autostart_is_enabled())
                autostart_disable();
            else
                autostart_enable();
            break;
        case ID_EXIT:
            PostMessageW(h, WM_CLOSE, 0, 0);
            break;
        }
        return 0;
    }
    if (message == WM_TIMER) {
        DWORD mask = GetLogicalDrives();
        if (mask != last_mask) {
            last_mask = mask;
            if (!paused)
                start_sync();
        }
        return 0;
    }
    if (message == WM_SYNC_DONE) {
        WaitForSingleObject(worker, INFINITE);
        CloseHandle(worker);
        worker = NULL;
        tip(paused   ? L"AudioSync - paused"
            : w == 0 ? L"AudioSync - up to date"
            : w == 3 ? L"AudioSync - no mounted profile"
                     : L"AudioSync - sync failed (see log)");
        if (pending && !paused) {
            pending = 0;
            start_sync();
        }
        return 0;
    }
    if (message == WM_CLOSE) {
        stopping = 1;
        KillTimer(h, 1);
        /* Finish the active transfer before releasing the process locks. */
        if (worker) {
            log_info("Waiting for the active sync before exiting");
            WaitForSingleObject(worker, INFINITE);
            CloseHandle(worker);
            worker = NULL;
        }
        if (with_tray)
            Shell_NotifyIconW(NIM_DELETE, &icon);
        DestroyWindow(h);
        return 0;
    }
    if (message == WM_DESTROY) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, message, w, l);
}
int tray_run(int headless) {
    with_tray = !headless;
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"AudioSyncHidden";
    if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return 1;
    window =
        CreateWindowExW(0, wc.lpszClassName, L"AudioSync", 0, 0, 0, 0, 0, NULL, NULL, wc.hInstance, NULL);
    if (!window)
        return 1;
    memset(&icon, 0, sizeof icon);
    icon.cbSize = sizeof icon;
    icon.hWnd = window;
    icon.uID = 1;
    icon.uFlags = NIF_MESSAGE | NIF_TIP | NIF_ICON;
    icon.uCallbackMessage = WM_TRAYICON;
    icon.hIcon = LoadIconW(NULL, IDI_APPLICATION);
    wcscpy(icon.szTip, L"AudioSync");
    if (with_tray && !Shell_NotifyIconW(NIM_ADD, &icon)) {
        log_err("Tray icon unavailable; use watch --no-tray for headless operation");
        DestroyWindow(window);
        return 1;
    }
    if (with_tray) {
        DWORD processes[2];
        if (GetConsoleProcessList(processes, 2) == 1)
            FreeConsole();
    }
    SetConsoleCtrlHandler(control, TRUE);
    last_mask = GetLogicalDrives();
    if (!SetTimer(window, 1, 5000, NULL)) {
        SendMessageW(window, WM_CLOSE, 0, 0);
        return 1;
    }
    log_info("Watching mounted drives every 5 seconds");
    start_sync();
    MSG message;
    int result;
    while ((result = GetMessageW(&message, NULL, 0, 0)) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    SetConsoleCtrlHandler(control, FALSE);
    return result < 0 ? 1 : 0;
}
static const wchar_t *RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
int autostart_is_enabled(void) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return 0;
    DWORD size = 0;
    LONG result = RegQueryValueExW(key, L"AudioSync", NULL, NULL, NULL, &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS;
}
int autostart_enable(void) {
    wchar_t exe[1024], command[1100];
    DWORD length = GetModuleFileNameW(NULL, exe, 1024);
    if (!length || length >= 1024)
        return 1;
    _snwprintf(command, 1100, L"\"%s\" watch", exe);
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL) !=
        ERROR_SUCCESS)
        return 1;
    LONG result = RegSetValueExW(key, L"AudioSync", 0, REG_SZ, (const BYTE *)command,
                                 (DWORD)((wcslen(command) + 1) * sizeof *command));
    RegCloseKey(key);
    return result != ERROR_SUCCESS;
}
int autostart_disable(void) {
    HKEY key;
    LONG result = RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &key);
    if (result == ERROR_FILE_NOT_FOUND)
        return 0;
    if (result != ERROR_SUCCESS)
        return 1;
    result = RegDeleteValueW(key, L"AudioSync");
    RegCloseKey(key);
    return result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND;
}
