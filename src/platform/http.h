#ifndef HTTP_H
#define HTTP_H
#include <stddef.h>
#include <wchar.h>

int http_get(const char *url, const char *bearer, char **out, size_t *outlen, int *status);
int http_post_json(const char *url, const char *body, const char *bearer, char **out, size_t *outlen,
                   int *status);

int http_download(const char *url, const char *bearer, const wchar_t *wpath);

typedef void (*http_progress_cb)(unsigned long long done, unsigned long long total, void *ctx);
int http_download_ex(const char *url, const char *bearer, const wchar_t *wpath, http_progress_cb cb,
                     void *ctx);

#endif
