#include "platform/devices.h"
#include "platform/util.h"

const drive_info_t *devices_find_by_serial(const drive_info_t *ds, int n, const char *serial) {
    for (int i = 0; i < n; i++)
        if (text_equal_ci(ds[i].serial, serial))
            return &ds[i];
    return NULL;
}
