#include "cli/cli.h"
#include "platform/detach.h"
#include <locale.h>
#ifdef _WIN32
#include "platform/windows/crashlog.h"
#include "platform/util.h"
#include <windows.h>
#include <shellapi.h>
#include <stdlib.h>
#else
#include <curl/curl.h>
#endif
int main(int argc, char **argv) {
    setlocale(LC_ALL, "");
    detach_prepare();
#ifdef _WIN32
    crashlog_install();
    SetConsoleOutputCP(CP_UTF8);
    HRESULT cohr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    int count;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    char **utf8 = wide ? calloc((size_t)count + 1, sizeof *utf8) : NULL;
    if (!utf8)
        return 1;
    for (int i = 0; i < count; ++i) {
        utf8[i] = wide_to_utf8(wide[i]);
        if (!utf8[i])
            return 1;
    }
    argc = count;
    argv = utf8;
#else
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
#endif
    int result = cli_run(argc, argv);
#ifdef _WIN32
    for (int i = 0; i < count; ++i)
        free(utf8[i]);
    free(utf8);
    LocalFree(wide);
    if (SUCCEEDED(cohr))
        CoUninitialize();
#else
    curl_global_cleanup();
#endif
    return result;
}
