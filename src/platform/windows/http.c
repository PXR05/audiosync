#include "platform/http.h"
#include "log.h"
#include "platform/util.h"
#include <windows.h>
#include <winhttp.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    wchar_t *host;
    wchar_t *path;
    INTERNET_PORT port;
    int secure;
} urlparts_t;

static int crack(const char *url, urlparts_t *u) {
    memset(u, 0, sizeof *u);
    wchar_t *wurl = utf8_to_wide(url);
    if (!wurl)
        return -1;
    URL_COMPONENTSW c;
    memset(&c, 0, sizeof c);
    c.dwStructSize = sizeof c;
    wchar_t host[256] = {0}, path[2048] = {0};
    c.lpszHostName = host;
    c.dwHostNameLength = 256;
    c.lpszUrlPath = path;
    c.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(wurl, 0, 0, &c)) {
        free(wurl);
        return -1;
    }
    u->host = utf8_to_wide("");
    free(u->host);
    u->host = (wchar_t *)malloc((wcslen(host) + 1) * sizeof(wchar_t));
    u->path = (wchar_t *)malloc((wcslen(path) + 1 + 8) * sizeof(wchar_t));
    if (!u->host || !u->path) {
        free(wurl);
        free(u->host);
        free(u->path);
        return -1;
    }
    wcscpy(u->host, host);
    wcscpy(u->path, path);
    if (c.lpszExtraInfo && *c.lpszExtraInfo)
        wcscat(u->path, c.lpszExtraInfo);
    u->port = c.nPort;
    u->secure = (c.nScheme == INTERNET_SCHEME_HTTPS);
    free(wurl);
    return 0;
}

typedef struct {
    const char *method;
    const char *body;
    const char *bearer;
    char *data;
    size_t len, cap;
    int status;
    const wchar_t *save_to;
    http_progress_cb pcb;
    void *pctx;
} req_t;

static int do_req(urlparts_t *u, req_t *r, const wchar_t *verb_w) {
    int rc = -1;
    HINTERNET hS = NULL, hC = NULL, hR = NULL;
    hS = WinHttpOpen(L"audiosync/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hS)
        goto out;
    WinHttpSetTimeouts(hS, 15000, 15000, 30000, 60000);
    hC = WinHttpConnect(hS, u->host, u->port, 0);
    if (!hC)
        goto out;
    DWORD flags = u->secure ? WINHTTP_FLAG_SECURE : 0;
    hR = WinHttpOpenRequest(hC, verb_w, u->path, NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                            flags);
    if (!hR)
        goto out;
    if (r->bearer && *r->bearer) {
        char ah[512];
        snprintf(ah, sizeof ah, "Authorization: Bearer %s", r->bearer);
        wchar_t *w = utf8_to_wide(ah);
        WinHttpAddRequestHeaders(hR, w, (ULONG)-1, WINHTTP_ADDREQ_FLAG_ADD);
        free(w);
    }
    LPCWSTR ctype = NULL;
    wchar_t ctype_buf[64] = {0};
    DWORD body_len = 0;
    LPVOID body_p = WINHTTP_NO_REQUEST_DATA;
    if (r->body) {
        wcscpy(ctype_buf, L"Content-Type: application/json");
        ctype = ctype_buf;
        body_len = (DWORD)strlen(r->body);
        body_p = (LPVOID)r->body;
    }
    if (!WinHttpSendRequest(hR, ctype, body_p ? (DWORD)-1 : 0, body_p, body_len, body_len, 0))
        goto out;
    if (!WinHttpReceiveResponse(hR, NULL))
        goto out;
    DWORD sc = 0, scl = sizeof sc;
    WinHttpQueryHeaders(hR, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &sc, &scl, WINHTTP_NO_HEADER_INDEX);
    r->status = (int)sc;
    unsigned long long total = 0;
    {
        DWORD cl = 0;
        DWORD cll = sizeof cl;
        if (WinHttpQueryHeaders(hR, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &cl, &cll, WINHTTP_NO_HEADER_INDEX))
            total = cl;
    }
    HANDLE hf = INVALID_HANDLE_VALUE;
    if (r->save_to) {
        hf = CreateFileW(r->save_to, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hf == INVALID_HANDLE_VALUE)
            goto out;
    }
    unsigned long long written = 0;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hR, &avail)) {
            if (hf != INVALID_HANDLE_VALUE)
                CloseHandle(hf);
            goto out;
        }
        if (avail == 0)
            break;
        char *buf = (char *)malloc(avail);
        if (!buf) {
            if (hf != INVALID_HANDLE_VALUE)
                CloseHandle(hf);
            goto out;
        }
        DWORD got = 0;
        if (!WinHttpReadData(hR, buf, avail, &got)) {
            free(buf);
            if (hf != INVALID_HANDLE_VALUE)
                CloseHandle(hf);
            goto out;
        }
        if (hf != INVALID_HANDLE_VALUE) {
            DWORD w = 0;
            WriteFile(hf, buf, got, &w, NULL);
            free(buf);
            if (w != got) {
                CloseHandle(hf);
                DeleteFileW(r->save_to);
                goto out;
            }
            written += got;
            if (r->pcb)
                r->pcb(written, total, r->pctx);
        } else {
            if (r->len + got + 1 > r->cap) {
                size_t nc = r->cap ? r->cap * 2 : 65536;
                while (nc < r->len + got + 1)
                    nc *= 2;
                char *nd = (char *)realloc(r->data, nc);
                if (!nd) {
                    free(buf);
                    goto out;
                }
                r->data = nd;
                r->cap = nc;
            }
            memcpy(r->data + r->len, buf, got);
            r->len += got;
            free(buf);
        }
    }
    if (hf != INVALID_HANDLE_VALUE)
        CloseHandle(hf);
    if (r->data)
        r->data[r->len] = 0;
    rc = 0;
out:
    if (hR)
        WinHttpCloseHandle(hR);
    if (hC)
        WinHttpCloseHandle(hC);
    if (hS)
        WinHttpCloseHandle(hS);
    return rc;
}

static int req_simple(const char *method_w_unused, const wchar_t *verb, const char *url, const char *body,
                      const char *bearer, char **out, size_t *outlen, int *status, const wchar_t *save,
                      http_progress_cb pcb, void *pctx) {
    (void)method_w_unused;
    urlparts_t u;
    if (crack(url, &u) != 0) {
        log_err("bad url: %s", url);
        return -1;
    }
    req_t r;
    memset(&r, 0, sizeof r);
    r.body = body;
    r.bearer = bearer;
    r.save_to = save;
    r.pcb = pcb;
    r.pctx = pctx;
    int rc = do_req(&u, &r, verb);
    free(u.host);
    free(u.path);
    if (rc != 0) {
        free(r.data);
        return -1;
    }
    if (status)
        *status = r.status;
    if (out)
        *out = r.data;
    else
        free(r.data);
    if (outlen)
        *outlen = r.len;
    return 0;
}

int http_get(const char *url, const char *bearer, char **out, size_t *outlen, int *status) {
    return req_simple(NULL, L"GET", url, NULL, bearer, out, outlen, status, NULL, NULL, NULL);
}
int http_post_json(const char *url, const char *body, const char *bearer, char **out, size_t *outlen,
                   int *status) {
    return req_simple(NULL, L"POST", url, body, bearer, out, outlen, status, NULL, NULL, NULL);
}
int http_download(const char *url, const char *bearer, const wchar_t *wpath) {
    return http_download_ex(url, bearer, wpath, NULL, NULL);
}
int http_download_ex(const char *url, const char *bearer, const wchar_t *wpath, http_progress_cb cb,
                     void *ctx) {
    int status = 0;
    if (req_simple(NULL, L"GET", url, NULL, bearer, NULL, NULL, &status, wpath, cb, ctx) != 0)
        return -1;
    return (status == 200 || status == 206) ? 0 : -2;
}
