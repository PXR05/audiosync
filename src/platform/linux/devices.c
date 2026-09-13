#define _GNU_SOURCE
#include "platform/devices.h"
#include <stdio.h>
#include <string.h>
#include <gio/gio.h>
#include <sys/statvfs.h>
int devices_list(drive_info_t *out, int cap) {
    GVolumeMonitor *monitor = g_volume_monitor_get();
    GList *mounts = g_volume_monitor_get_mounts(monitor);
    int count = 0;
    for (GList *item = mounts; item && count < cap; item = item->next) {
        GMount *mount = G_MOUNT(item->data);
        GFile *root = g_mount_get_root(mount);
        char *path = g_file_get_path(root);
        char *uuid = g_mount_get_uuid(mount);
        char *name = g_mount_get_name(mount);
        if (path && strcmp(path, "/") && strncmp(path, "/boot", 5)) {
            drive_info_t *drive = &out[count];
            memset(drive, 0, sizeof *drive);
            snprintf(drive->mount, sizeof drive->mount, "%s", path);
            snprintf(drive->label, sizeof drive->label, "%s", name ? name : path);
            snprintf(drive->serial, sizeof drive->serial, "%s", uuid && *uuid ? uuid : path);
            struct statvfs info;
            if (statvfs(path, &info) == 0) {
                drive->total = (unsigned long long)info.f_blocks * info.f_frsize;
                drive->avail = (unsigned long long)info.f_bavail * info.f_frsize;
            }
            drive->drive_type = 2;
            count++;
        }
        g_free(path);
        g_free(uuid);
        g_free(name);
        g_object_unref(root);
    }
    g_list_free_full(mounts, g_object_unref);
    g_object_unref(monitor);
    return count;
}
