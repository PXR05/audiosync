#include "platform/runtime.h"
#include "sync.h"
#include "status.h"
#include "progress.h"
#include "log.h"
#include <stddef.h>

int runtime_sync(const char *only) {
    if (!runtime_lock(LOCK_SYNC)) {
        log_err("Another sync is running or unavailable; use 'audiosync status -w' to view progress");
        return 1;
    }
    status_begin_run();
    progress_observe(&progress_status);
    int result = sync_all(only);
    result = result == 99 ? 3 : result ? 1 : 0;
    progress_observe(NULL);
    status_end_run(result);
    runtime_unlock(LOCK_SYNC);
    return result;
}
