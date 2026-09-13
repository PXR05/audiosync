#include "platform/update.h"
#include "platform/http.h"
#include "platform/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

const char *update_asset(void) {
#if defined(__aarch64__)
    return "aarch64.AppImage";
#else
    return "x86_64.AppImage";
#endif
}

int update_install(const char *url, const char *version) {
    (void)version;
    const char *app = getenv("APPIMAGE");
    if (!app || !*app)
        return -2;
    size_t length = strlen(app) + 8;
    char *temp = malloc(length);
    if (!temp)
        return -1;
    snprintf(temp, length, "%s.update", app);
    wchar_t *wide = utf8_to_wide(temp);
    int result = !wide || http_download(url, NULL, wide) || chmod(temp, 0755) || rename(temp, app);
    if (result)
        unlink(temp);
    free(wide);
    free(temp);
    return result ? -1 : 0;
}
