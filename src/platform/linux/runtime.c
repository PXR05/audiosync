#define _GNU_SOURCE
#include "platform/runtime.h"
#include "platform/files.h"
#include <stdio.h>
#include <stdlib.h>

#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
static int locks[LOCK_COUNT] = {-1, -1, -1};
static int open_lock(int slot, int create) {
    char name[32];
    snprintf(name, sizeof name, ".lock-%d", slot);
    char *path = state_path(name, create);
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    int fd = open(path, O_RDWR | O_CLOEXEC | O_NOFOLLOW | (create ? O_CREAT : 0), 0600);
    free(path);
    return fd;
}
int runtime_lock(int slot) {
    int fd = open_lock(slot, 1);
    if (fd < 0)
        return 0;
    if (flock(fd, LOCK_EX | LOCK_NB)) {
        close(fd);
        return 0;
    }
    locks[slot] = fd;
    return 1;
}
void runtime_unlock(int slot) {
    flock(locks[slot], LOCK_UN);
    close(locks[slot]);
    locks[slot] = -1;
}
int runtime_busy(int slot) {
    int fd = open_lock(slot, 0);
    if (fd < 0)
        return errno == ENOENT ? 0 : -1;
    int result = flock(fd, LOCK_EX | LOCK_NB);
    int error = errno;
    if (!result)
        flock(fd, LOCK_UN);
    close(fd);
    return result ? (error == EWOULDBLOCK || error == EAGAIN ? 1 : -1) : 0;
}
