#ifndef PROGRESS_H
#define PROGRESS_H

typedef struct {
    void (*stage)(const char *msg);
    void (*begin)(int total);
    void (*file)(int idx, int total, const char *rel);
    void (*bytes)(unsigned long long done, unsigned long long total);
    void (*convert)(const char *rel);
    void (*end)(int fresh, int skipped, int failed);
} progress_reporter_t;

void progress_set(const progress_reporter_t *r);
void progress_observe(const progress_reporter_t *r);
void progress_stage(const char *fmt, ...);
void progress_begin(int total);
void progress_file(int idx, int total, const char *rel);
void progress_bytes(unsigned long long done, unsigned long long total);
void progress_convert(const char *rel);
void progress_end(int fresh, int skipped, int failed);

extern const progress_reporter_t progress_cli;

#endif
