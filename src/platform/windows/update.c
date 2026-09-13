#include "platform/update.h"
#include "platform/http.h"
#include "platform/util.h"
#include <stdlib.h>
#include <windows.h>
#include <shellapi.h>

const char *update_asset(void) {
    return "windows-x64-setup.exe";
}

int update_install(const char *url, const char *version) {
    wchar_t temp[MAX_PATH], path[MAX_PATH];
    wchar_t *wide_version = utf8_to_wide(version);
    if (!wide_version || !GetTempPathW(MAX_PATH, temp) ||
        _snwprintf(path, MAX_PATH, L"%sAudioSync-%s-setup.exe", temp, wide_version) < 0) {
        free(wide_version);
        return -1;
    }
    free(wide_version);
    if (http_download(url, NULL, path))
        return -1;
    HINSTANCE process =
        ShellExecuteW(NULL, L"open", path, L"/SP- /SILENT /SUPPRESSMSGBOXES /NORESTART", NULL, SW_SHOWNORMAL);
    return (INT_PTR)process > 32 ? 0 : -1;
}
