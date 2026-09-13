/* A controlled transfer for cross-process status tests; never touches destination files. */
#include "platform/runtime.h"
#include "progress.h"
#include "log.h"
#include <stdio.h>
int sync_all(const char *only) {
    (void)only;
    log_info("fixture: transfer started");
    progress_stage("Fixture transfer");
    progress_begin(2);
    progress_file(1, 2, "Album/song.flac");
    progress_bytes(512, 1024);
    progress_stage("Copying audio");
    puts("ready");
    fflush(stdout);
    if (getchar() == EOF)
        return -1;
    progress_end(1, 1, 0);
    log_info("fixture: transfer complete");
    return 0;
}
int main(void) {
    return runtime_sync(NULL);
}
