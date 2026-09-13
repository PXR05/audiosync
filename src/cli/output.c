#include "cli/internal.h"
#include "json.h"
#include "platform/devices.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif
int cli_is_terminal(FILE *stream) {
#ifdef _WIN32
    return _isatty(_fileno(stream));
#else
    return isatty(fileno(stream));
#endif
}
static void member(const char *name, const char *value) {
    json_write_string(stdout, name);
    putchar(':');
    json_write_string(stdout, value);
}
void cli_print_profile(const device_cfg_t *d, int json, int connected) {
    if (json) {
        putchar('{');
        member("name", d->name);
        putchar(',');
        member("serial", d->serial);
        putchar(',');
        member("label", d->label);
        putchar(',');
        member("source_type", d->source_type);
        putchar(',');
        member("source", !strcmp(d->source_type, "local") ? d->local_path : d->base_url);
        putchar(',');
        member("target", d->target_subdir);
        putchar(',');
        member("format", d->format);
        printf(",\"layout\":\"%c\",\"mirror\":%s,\"connected\":%s}", layout_to_char(d->layout),
               d->mirror ? "true" : "false", connected ? "true" : "false");
    } else {
        printf("%s  [%s]\n  Serial:  %s\n  Source:  %s\n  Target:  %s\n  Output:  %s | layout %c | %s\n",
               d->name, connected ? "connected" : "offline", d->serial,
               !strcmp(d->source_type, "local") ? d->local_path : d->base_url, d->target_subdir, d->format,
               layout_to_char(d->layout), d->mirror ? "mirror" : "incremental");
    }
}
int cli_devices(int json) {
    drive_info_t drives[64];
    int count = devices_list(drives, 64);
    if (json)
        putchar('[');
    else if (!count)
        puts("No mounted drives found. Connect a drive and try again.");
    for (int i = 0; i < count; ++i) {
        drive_info_t *d = &drives[i];
        char mount[1024];
#ifdef _WIN32
        snprintf(mount, sizeof mount, "%c:\\", d->letter);
#else
        snprintf(mount, sizeof mount, "%s", d->mount);
#endif
        if (json) {
            if (i)
                putchar(',');
            putchar('{');
            member("serial", d->serial);
            putchar(',');
            member("mount", mount);
            putchar(',');
            member("label", d->label);
            printf(",\"free_bytes\":%llu,\"total_bytes\":%llu}", d->avail, d->total);
        } else
            printf("%s  %s\n  Serial: %s\n  Free:   %.1f GiB / %.1f GiB\n", mount,
                   d->label[0] ? d->label : "(no label)", d->serial, (double)d->avail / 1073741824.0,
                   (double)d->total / 1073741824.0);
    }
    if (json)
        puts("]");
    return 0;
}
