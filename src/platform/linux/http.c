#ifndef _WIN32
#include "platform/http.h"
#include "platform/util.h"
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *data;
    size_t length;
} memory_t;
typedef struct {
    FILE *file;
    http_progress_cb callback;
    void *context;
} download_t;

static size_t write_memory(void *data, size_t size, size_t count, void *context) {
    memory_t *memory = context;
    size_t bytes = size * count;
    char *next = realloc(memory->data, memory->length + bytes + 1);
    if (!next)
        return 0;
    memory->data = next;
    memcpy(memory->data + memory->length, data, bytes);
    memory->length += bytes;
    memory->data[memory->length] = 0;
    return bytes;
}

static size_t write_file(void *data, size_t size, size_t count, void *context) {
    return fwrite(data, size, count, ((download_t *)context)->file) * size;
}

static int progress(void *context, curl_off_t total, curl_off_t done, curl_off_t upload_total,
                    curl_off_t upload_done) {
    (void)upload_total;
    (void)upload_done;
    download_t *download = context;
    if (download->callback)
        download->callback((unsigned long long)done, (unsigned long long)total, download->context);
    return 0;
}

static struct curl_slist *headers(const char *bearer, int json) {
    struct curl_slist *list = NULL;
    if (json)
        list = curl_slist_append(list, "Content-Type: application/json");
    if (bearer && *bearer) {
        size_t size = strlen(bearer) + 24;
        char *value = malloc(size);
        if (value) {
            snprintf(value, size, "Authorization: Bearer %s", bearer);
            list = curl_slist_append(list, value);
            free(value);
        }
    }
    return list;
}

static int request(const char *url, const char *body, const char *bearer, memory_t *memory,
                   download_t *download, int *status) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return -1;
    struct curl_slist *list = headers(bearer, body != NULL);
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, list);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "audiosync/1.0");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    if (body)
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
    if (download) {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_file);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, download);
        curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, progress);
        curl_easy_setopt(curl, CURLOPT_XFERINFODATA, download);
        curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, memory);
    }
    CURLcode result = curl_easy_perform(curl);
    long code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    if (status)
        *status = (int)code;
    curl_slist_free_all(list);
    curl_easy_cleanup(curl);
    return result == CURLE_OK ? 0 : -1;
}

int http_get(const char *url, const char *bearer, char **out, size_t *outlen, int *status) {
    memory_t memory = {0};
    int result = request(url, NULL, bearer, &memory, NULL, status);
    if (result)
        free(memory.data);
    else {
        if (out)
            *out = memory.data;
        else
            free(memory.data);
        if (outlen)
            *outlen = memory.length;
    }
    return result;
}
int http_post_json(const char *url, const char *body, const char *bearer, char **out, size_t *outlen,
                   int *status) {
    memory_t memory = {0};
    int result = request(url, body, bearer, &memory, NULL, status);
    if (result)
        free(memory.data);
    else {
        if (out)
            *out = memory.data;
        else
            free(memory.data);
        if (outlen)
            *outlen = memory.length;
    }
    return result;
}
int http_download(const char *url, const char *bearer, const wchar_t *path) {
    return http_download_ex(url, bearer, path, NULL, NULL);
}
int http_download_ex(const char *url, const char *bearer, const wchar_t *wide_path, http_progress_cb callback,
                     void *context) {
    char *path = wide_to_utf8(wide_path);
    if (!path)
        return -1;
    for (char *p = path; *p; ++p)
        if (*p == '\\')
            *p = '/';
    FILE *file = fopen(path, "wb");
    if (!file) {
        free(path);
        return -1;
    }
    download_t download = {file, callback, context};
    int status = 0, result = request(url, NULL, bearer, NULL, &download, &status);
    if (fclose(file) != 0)
        result = -1;
    if (result || (status != 200 && status != 206)) {
        remove(path);
        if (!result)
            result = -2;
    }
    free(path);
    return result;
}
#endif
