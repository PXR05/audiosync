#include "cli/internal.h"
#include "json.h"
#include "platform/http.h"
#include "platform/update.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef AUDIOSYNC_VERSION
#define AUDIOSYNC_VERSION "dev"
#endif

static int version(const char *text, unsigned out[3]) {
    char extra;
    return sscanf(text, "%u.%u.%u%c", out, out + 1, out + 2, &extra) == 3;
}

static int newer(const char *available) {
    unsigned current[3], latest[3];
    if (!version(AUDIOSYNC_VERSION, current) || !version(available, latest))
        return -1;
    for (int i = 0; i < 3; ++i)
        if (current[i] != latest[i])
            return latest[i] > current[i];
    return 0;
}

static int latest_version(char out[32]) {
    const char *api = getenv("AUDIOSYNC_UPDATE_API");
    if (!api || !*api)
        api = "https://api.github.com/repos/PXR05/audiosync/releases/latest";
    char *response = NULL;
    size_t length = 0;
    int status = 0;
    if (http_get(api, NULL, &response, &length, &status) || status != 200 || !response)
        return free(response), -1;
    jtok_t tokens[1024];
    int count = length > 0xffffffffu ? -1 : json_parse(response, (unsigned)length, tokens, 1024);
    int tag = count > 0 ? json_obj_get(response, tokens, count, 0, "tag_name") : -1;
    int result = json_str(response, tokens, tag, out, 32);
    free(response);
    if (result < 2 || out[0] != 'v')
        return -1;
    memmove(out, out + 1, strlen(out));
    return version(out, (unsigned[3]){0}) ? 0 : -1;
}

int cli_update(int argc, char **argv, int quiet) {
    int check = argc == 3 && !strcmp(argv[2], "--check");
    if (argc != 2 && !check)
        return cli_error("update", "expected update [--check]");
    char latest[32];
    if (latest_version(latest))
        return fputs("error: could not check for updates\n", stderr), 1;
    int available = newer(latest);
    if (available < 0)
        return fputs("error: invalid application version\n", stderr), 1;
    if (!available) {
        if (!quiet)
            printf("AudioSync %s is up to date.\n", AUDIOSYNC_VERSION);
        return 0;
    }
    if (check) {
        printf("AudioSync %s is available; installed version is %s.\n", latest, AUDIOSYNC_VERSION);
        return 0;
    }
    char url[512];
    snprintf(url, sizeof url, "https://github.com/PXR05/audiosync/releases/download/v%s/AudioSync-%s-%s",
             latest, latest, update_asset());
    if (!quiet)
        printf("Downloading AudioSync %s...\n", latest);
    int result = update_install(url, latest);
    if (result == -2)
        fputs("error: automatic updates require the AppImage or Windows installer\n", stderr);
    else if (result)
        fputs("error: could not install the update\n", stderr);
    else if (!quiet)
        puts("Update started. The new version will be used the next time AudioSync runs.");
    return result ? 1 : 0;
}
