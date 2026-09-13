#include "organizer.h"
#include <string.h>
#include <stdio.h>

void sanitize_component(char *io) {
    for (char *p = io; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' || c == '|' ||
            c == '?' || c == '*')
            *p = '_';
    }
    size_t n = strlen(io);
    while (n > 0 && (io[n - 1] == '.' || io[n - 1] == ' '))
        io[--n] = 0;
    if (n == 0)
        strcpy(io, "Unknown");
    if (n > 100) {
        io[100] = 0;
    }
}

const char *file_ext(const char *filename) {
    const char *dot = strrchr(filename, '.');
    const char *sep = strrchr(filename, '/');
    if (!dot || (sep && dot < sep))
        return "";
    return dot + 1;
}

int num_width(int count) {
    int w = 2, t = count;
    while (t >= 100) {
        w++;
        t /= 10;
    }
    return w;
}

static void safe_copy(char *d, size_t cap, const char *s, const char *fb) {
    char tmp[256];
    snprintf(tmp, sizeof tmp, "%s", (s && *s) ? s : fb);
    sanitize_component(tmp);
    size_t length = strnlen(tmp, cap - 1);
    memcpy(d, tmp, length);
    d[length] = 0;
}

void layout_d_path(char *dst, size_t cap, const char *album, int track_no, int width, const char *artist,
                   const char *title, const char *ext) {
    char a[128], ar[128], ti[256];
    safe_copy(a, sizeof a, album, "Unknown Album");
    safe_copy(ar, sizeof ar, artist, "Unknown Artist");
    safe_copy(ti, sizeof ti, title, "Unknown Title");
    if (width < 2)
        width = 2;
    snprintf(dst, cap, "%s/%0*d. %s - %s.%s", a, width, track_no, ar, ti, (ext && *ext) ? ext : "opus");
}

void layout_a_path(char *dst, size_t cap, const char *artist, const char *album, const char *title,
                   const char *ext) {
    char a[128], ar[128], ti[256];
    safe_copy(ar, sizeof ar, artist, "Unknown Artist");
    safe_copy(a, sizeof a, album, "Unknown Album");
    safe_copy(ti, sizeof ti, title, "Unknown Title");
    snprintf(dst, cap, "%s/%s/%s.%s", ar, a, ti, (ext && *ext) ? ext : "opus");
}

void layout_b_path(char *dst, size_t cap, const char *artist, const char *title, const char *id,
                   const char *ext) {
    char ar[128], ti[256];
    safe_copy(ar, sizeof ar, artist, "Unknown Artist");
    safe_copy(ti, sizeof ti, title, "Unknown Title");
    snprintf(dst, cap, "%s - %s (%s).%s", ar, ti, id ? id : "x", (ext && *ext) ? ext : "opus");
}
