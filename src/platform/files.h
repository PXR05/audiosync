#ifndef AUDIOSYNC_FILES_H
#define AUDIOSYNC_FILES_H
#include <stdio.h>
char *state_path(const char *name, int create_directory);
FILE *file_open_utf8(const char *path, const char *mode);
int file_replace_utf8(const char *from, const char *to);
#endif
