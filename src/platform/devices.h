#ifndef DEVICES_H
#define DEVICES_H

typedef struct {
    char letter;
    char label[64];
    char serial[128];
    char mount[1024];
    unsigned long long total, avail;
    int drive_type;
} drive_info_t;

int devices_list(drive_info_t *out, int cap);
const drive_info_t *devices_find_by_serial(const drive_info_t *ds, int n, const char *serial);

#endif
