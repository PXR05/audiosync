#ifndef LOG_H
#define LOG_H
#include <stdio.h>
void log_info(const char *fmt, ...);
void log_warn(const char *fmt, ...);
void log_err(const char *fmt, ...);

void log_set_quiet(int enabled);
#endif
