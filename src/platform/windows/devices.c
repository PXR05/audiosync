#include "platform/devices.h"
#include <windows.h>
#include <stdio.h>
#include <string.h>

int devices_list(drive_info_t *out, int cap) {
    int n = 0;
    DWORD mask = GetLogicalDrives();
    for (char L = 'A'; L <= 'Z' && n < cap; L++) {
        if (!(mask & (1u << (L - 'A'))))
            continue;
        wchar_t root[4] = {(wchar_t)L, L':', L'\\', 0};
        UINT dt = GetDriveTypeW(root);
        if (dt != DRIVE_REMOVABLE && dt != DRIVE_FIXED)
            continue;
        wchar_t vname[MAX_PATH] = {0}, fsname[32] = {0};
        DWORD serial = 0, maxcomp = 0, flags = 0;
        if (!GetVolumeInformationW(root, vname, sizeof(vname) / sizeof(vname[0]), &serial, &maxcomp, &flags,
                                   fsname, 32))
            continue;
        ULARGE_INTEGER avail, total, free2;
        if (!GetDiskFreeSpaceExW(root, &avail, &total, &free2))
            continue;
        drive_info_t *d = &out[n++];
        memset(d, 0, sizeof *d);
        d->letter = L;
        WideCharToMultiByte(CP_UTF8, 0, vname, -1, d->label, sizeof d->label, NULL, NULL);
        snprintf(d->serial, sizeof d->serial, "%08lX", (unsigned long)serial);
        d->total = total.QuadPart;
        d->avail = avail.QuadPart;
        d->drive_type = (int)dt;
    }
    return n;
}
