#ifndef AUDIOSYNC_RUNTIME_H
#define AUDIOSYNC_RUNTIME_H
enum { LOCK_WATCH, LOCK_SYNC, LOCK_CONFIG, LOCK_COUNT };
int runtime_lock(int slot);
void runtime_unlock(int slot);
/* 1 busy, 0 idle, -1 inaccessible. Does not create lock files. */
int runtime_busy(int slot);
int runtime_sync(const char *only);
#endif
