#ifndef SYNC_H
#define SYNC_H
#include "config.h"
#include "platform/devices.h"

int sync_device(const device_cfg_t *dev, const drive_info_t *drives, int ndrives);

int sync_all(const char *only);

int sync_target_valid(const char *path);

#endif
