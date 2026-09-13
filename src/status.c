#include "status.h"
#include "platform/files.h"
#include "json.h"
#include "platform/util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static sync_status_t current;
static unsigned long long last_write;
static void publish(int force) {
    unsigned long long now = GetTickCount64();
    if (!force && now - last_write < 250)
        return;
    last_write = now;
    current.updated = (long long)time(NULL);
    char *path = state_path("status.json", 1), *temp = state_path("status.json.tmp", 0);
    FILE *f = path && temp ? file_open_utf8(temp, "wb") : NULL;
    if (f) {
        fprintf(f,
                "{\"pid\":%lld,\"updated\":%lld,\"result\":%d,\"index\":%d,\"total\":%d,"
                "\"fresh\":%d,\"skipped\":%d,\"failed\":%d,\"bytes\":%llu,\"bytes_total\":%llu,\"stage\":",
                current.pid, current.updated, current.result, current.index, current.total, current.fresh,
                current.skipped, current.failed, current.bytes, current.bytes_total);
        json_write_string(f, current.stage);
        fputs(",\"file\":", f);
        json_write_string(f, current.file);
        fputs("}\n", f);
        int ok = !ferror(f);
        if (fclose(f))
            ok = 0;
        if (ok)
            file_replace_utf8(temp, path);
    }
    free(temp);
    free(path);
}
void status_begin_run(void) {
    memset(&current, 0, sizeof current);
    current.pid = (long long)GetCurrentProcessId();
    current.result = -1;
    strcpy(current.stage, "Starting sync");
    publish(1);
}
void status_end_run(int result) {
    current.result = result;
    snprintf(current.stage, sizeof current.stage, "%s",
             result == 0   ? "Sync complete"
             : result == 3 ? "No matching mounted device"
                           : "Sync failed");
    publish(1);
}
static void stage(const char *text) {
    snprintf(current.stage, sizeof current.stage, "%s", text);
    publish(1);
}
static void begin(int total) {
    current.total = total;
    current.index = 0;
    current.bytes = current.bytes_total = 0;
    current.file[0] = 0;
    publish(1);
}
static void file(int index, int total, const char *text) {
    current.index = index;
    current.total = total;
    current.bytes = current.bytes_total = 0;
    strcpy(current.stage, "Copying audio");
    snprintf(current.file, sizeof current.file, "%s", text);
    publish(0);
}
static void bytes(unsigned long long done, unsigned long long total) {
    current.bytes = done;
    current.bytes_total = total;
    publish(0);
}
static void convert(const char *text) {
    snprintf(current.stage, sizeof current.stage, "Converting audio");
    snprintf(current.file, sizeof current.file, "%s", text);
    publish(1);
}
static void end(int fresh, int skipped, int failed) {
    current.fresh += fresh;
    current.skipped += skipped;
    current.failed += failed;
    publish(1);
}
const progress_reporter_t progress_status = {stage, begin, file, bytes, convert, end};
int status_read(sync_status_t *out) {
    memset(out, 0, sizeof *out);
    char *path = state_path("status.json", 0);
    FILE *f = file_open_utf8(path, "rb");
    free(path);
    if (!f)
        return 0;
    char data[16384];
    size_t n = fread(data, 1, sizeof data - 1, f);
    int complete = !ferror(f) && feof(f);
    fclose(f);
    if (!complete)
        return 0;
    data[n] = 0;
    jtok_t tokens[64];
    int count = json_parse(data, (unsigned)n, tokens, 64);
    if (count <= 0 || tokens[0].type != J_OBJ)
        return 0;
#define NUMBER(member)                                                                                       \
    do {                                                                                                     \
        int key = json_obj_get(data, tokens, count, 0, #member);                                             \
        if (key < 0 || tokens[key].type != J_NUM)                                                            \
            return 0;                                                                                        \
        out->member = json_int(data, tokens, key);                                                           \
    } while (0)
    NUMBER(pid);
    NUMBER(updated);
    NUMBER(result);
    NUMBER(index);
    NUMBER(total);
    NUMBER(fresh);
    NUMBER(skipped);
    NUMBER(failed);
    NUMBER(bytes);
    NUMBER(bytes_total);
#undef NUMBER
    int key = json_obj_get(data, tokens, count, 0, "stage");
    if (key < 0 || tokens[key].type != J_STR)
        return 0;
    json_str(data, tokens, key, out->stage, sizeof out->stage);
    key = json_obj_get(data, tokens, count, 0, "file");
    if (key < 0 || tokens[key].type != J_STR)
        return 0;
    json_str(data, tokens, key, out->file, sizeof out->file);
    return 1;
}
