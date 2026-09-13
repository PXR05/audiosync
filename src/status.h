#ifndef AUDIOSYNC_STATUS_H
#define AUDIOSYNC_STATUS_H
#include "progress.h"
typedef struct {
    long long pid, updated;
    int result, index, total, fresh, skipped, failed;
    unsigned long long bytes, bytes_total;
    char stage[512], file[1024];
} sync_status_t;
extern const progress_reporter_t progress_status;
void status_begin_run(void);
void status_end_run(int result);
int status_read(sync_status_t *status);
#endif
