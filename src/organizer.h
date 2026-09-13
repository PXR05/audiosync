#ifndef ORGANIZER_H
#define ORGANIZER_H
#include <stddef.h>

void sanitize_component(char *io);

const char *file_ext(const char *filename);

int num_width(int count);

void layout_d_path(char *dst, size_t cap, const char *album, int track_no, int width, const char *artist,
                   const char *title, const char *ext);

void layout_a_path(char *dst, size_t cap, const char *artist, const char *album, const char *title,
                   const char *ext);

void layout_b_path(char *dst, size_t cap, const char *artist, const char *title, const char *id,
                   const char *ext);

#endif
