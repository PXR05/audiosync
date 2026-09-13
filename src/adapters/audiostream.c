#include "adapters/adapter.h"
#include "adapters/builtin.h"
#include "platform/http.h"
#include "json.h"
#include "log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define TOKS_PAGE 8192
#define TOKS_BIG 65536

static void get_s(const char *js, const jtok_t *t, int nt, int obj, const char *key, char *out, size_t cap) {
    int v = json_obj_get(js, t, nt, obj, key);
    if (v < 0 || cap == 0) {
        if (cap)
            out[0] = 0;
        return;
    }
    if (t[v].type != J_STR) {
        out[0] = 0;
        return;
    }
    json_str(js, t, v, out, (unsigned)cap);
}

typedef struct {
    char base[256];
    char session_id[256];
} audiostream_session_t;

static void parse_track(const char *js, const jtok_t *t, int nt, int o, remote_track_t *tr) {
    memset(tr, 0, sizeof *tr);
    get_s(js, t, nt, o, "id", tr->id, sizeof tr->id);
    get_s(js, t, nt, o, "filename", tr->filename, sizeof tr->filename);
    get_s(js, t, nt, o, "uploadedAt", tr->uploaded_at, sizeof tr->uploaded_at);
    int sz = json_obj_get(js, t, nt, o, "size");
    tr->size = json_int(js, t, sz);
    int m = json_obj_get(js, t, nt, o, "metadata");
    if (m >= 0 && t[m].type == J_OBJ) {
        get_s(js, t, nt, m, "title", tr->title, sizeof tr->title);
        get_s(js, t, nt, m, "artist", tr->artist, sizeof tr->artist);
        get_s(js, t, nt, m, "album", tr->album, sizeof tr->album);
    }
}

static int login(const char *base, const char *user, const char *pass, char *sid_out, size_t cap) {
    char url[512], body[512];
    snprintf(url, sizeof url, "%s/auth/login", base);

    snprintf(body, sizeof body, "{\"username\":\"%s\",\"password\":\"%s\"}", user, pass);
    char *resp = NULL;
    size_t rlen = 0;
    int status = 0;
    if (http_post_json(url, body, NULL, &resp, &rlen, &status) != 0) {
        log_err("login: request failed");
        return -1;
    }
    int rc = -1;
    if (status == 200 && resp) {
        jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * 256);
        if (t) {
            int nt = json_parse(resp, (unsigned)rlen, t, 256);
            int v = nt > 0 ? json_obj_get(resp, t, nt, 0, "sessionId") : -1;
            if (v >= 0 && json_str(resp, t, v, sid_out, (unsigned)cap) > 0)
                rc = 0;
            else
                log_err("login: bad response (%d)", status);
            free(t);
        }
    } else
        log_err("login: http %d", status);
    free(resp);
    return rc;
}

static int fetch_tracks(const char *base, const char *sid, remote_track_t **out, int *n) {
    remote_track_t *arr = NULL;
    int cnt = 0, cap = 0, page = 1;
    for (;;) {
        char url[512];
        snprintf(url, sizeof url, "%s/audio?page=%d&limit=100", base, page);
        char *resp = NULL;
        size_t rlen = 0;
        int status = 0;
        if (http_get(url, sid, &resp, &rlen, &status) != 0 || status != 200 || !resp) {
            log_err("tracks: page %d failed (http %d)", page, status);
            free(resp);
            free(arr);
            return -1;
        }
        jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * TOKS_PAGE);
        if (!t) {
            free(resp);
            free(arr);
            return -1;
        }
        int nt = json_parse(resp, (unsigned)rlen, t, TOKS_PAGE);
        if (nt < 0) {
            log_err("tracks: json parse failed p%d", page);
            free(t);
            free(resp);
            free(arr);
            return -1;
        }
        int files = json_obj_get(resp, t, nt, 0, "files");
        int hasNext = json_obj_get(resp, t, nt, 0, "hasNext");
        int more = (hasNext >= 0) ? (int)json_int(resp, t, hasNext) : 0;
        if (files >= 0) {
            int fc = json_arr_len(t, files);
            for (int i = 0; i < fc; i++) {
                int o = json_arr_at(t, nt, files, i);
                if (o < 0)
                    continue;
                if (cnt >= cap) {
                    cap = cap ? cap * 2 : 256;
                    remote_track_t *na = (remote_track_t *)realloc(arr, (size_t)cap * sizeof *na);
                    if (!na) {
                        free(t);
                        free(resp);
                        free(arr);
                        return -1;
                    }
                    arr = na;
                }
                parse_track(resp, t, nt, o, &arr[cnt++]);
            }
        }
        log_info("tracks: page %d (+%d, total %d)", page, files >= 0 ? json_arr_len(t, files) : 0, cnt);
        free(t);
        free(resp);
        if (!more)
            break;
        page++;
        if (page > 1000)
            break;
    }
    *out = arr;
    *n = cnt;
    return 0;
}

static int cmp_item(const void *a, const void *b) {
    return ((const remote_playlist_item_t *)a)->position -
           ((const remote_playlist_item_t *)b)->position;
}

static int fetch_playlists(const char *base, const char *sid, remote_playlist_t **out, int *n) {
    char url[512];
    snprintf(url, sizeof url, "%s/playlist?limit=200", base);
    char *resp = NULL;
    size_t rlen = 0;
    int status = 0;
    if (http_get(url, sid, &resp, &rlen, &status) != 0 || status != 200 || !resp) {
        log_err("playlists: list failed (http %d)", status);
        free(resp);
        return -1;
    }
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * TOKS_BIG);
    if (!t) {
        free(resp);
        return -1;
    }
    int nt = json_parse(resp, (unsigned)rlen, t, TOKS_BIG);
    if (nt < 0) {
        free(t);
        free(resp);
        return -1;
    }
    int pls = json_obj_get(resp, t, nt, 0, "playlists");
    int pc = pls >= 0 ? json_arr_len(t, pls) : 0;
    remote_playlist_t *arr = (remote_playlist_t *)calloc((size_t)(pc > 0 ? pc : 1), sizeof *arr);
    if (!arr) {
        free(t);
        free(resp);
        return -1;
    }
    int got = 0;
    for (int i = 0; i < pc; i++) {
        int o = json_arr_at(t, nt, pls, i);
        char pid[128] = {0}, pname[256] = {0};
        get_s(resp, t, nt, o, "id", pid, sizeof pid);
        get_s(resp, t, nt, o, "name", pname, sizeof pname);
        if (!pid[0])
            continue;

        char durl[512];
        snprintf(durl, sizeof durl, "%s/playlist/%s", base, pid);
        char *dr = NULL;
        size_t dl = 0;
        int ds = 0;
        if (http_get(durl, sid, &dr, &dl, &ds) != 0 || ds != 200 || !dr) {
            log_warn("playlist '%s': detail failed", pname);
            free(dr);
            continue;
        }
        jtok_t *dt = (jtok_t *)malloc(sizeof(jtok_t) * TOKS_BIG);
        if (!dt) {
            free(dr);
            continue;
        }
        int dnt = json_parse(dr, (unsigned)dl, dt, TOKS_BIG);
        if (dnt < 0) {
            log_warn("playlist '%s': json fail", pname);
            free(dt);
            free(dr);
            continue;
        }
        int pl = json_obj_get(dr, dt, dnt, 0, "playlist");
        int items = pl >= 0 ? json_obj_get(dr, dt, dnt, pl, "items") : -1;
        int ic = items >= 0 ? json_arr_len(dt, items) : 0;
        remote_playlist_t *P = &arr[got];
        snprintf(P->id, sizeof P->id, "%s", pid);
        snprintf(P->name, sizeof P->name, "%s", pname[0] ? pname : "Unknown");
        P->items = (remote_playlist_item_t *)calloc((size_t)(ic > 0 ? ic : 1), sizeof *P->items);
        if (!P->items) {
            free(dt);
            free(dr);
            continue;
        }
        for (int k = 0; k < ic; k++) {
            int io = json_arr_at(dt, dnt, items, k);
            int pos = json_obj_get(dr, dt, dnt, io, "position");
            int au = json_obj_get(dr, dt, dnt, io, "audio");
            P->items[P->count].position = au >= 0 ? (int)json_int(dr, dt, pos) : k;
            if (au >= 0)
                parse_track(dr, dt, dnt, au, &P->items[P->count].track);
            P->count++;
        }
        qsort(P->items, (size_t)P->count, sizeof *P->items, cmp_item);
        got++;
        log_info("playlist '%s': %d tracks", P->name, P->count);
        free(dt);
        free(dr);
    }
    free(t);
    free(resp);
    *out = arr;
    *n = got;
    return 0;
}

static int download_track(const char *base, const char *sid, const char *id, const wchar_t *wpath,
                          remote_progress_cb cb, void *ctx) {
    char url[512];
    snprintf(url, sizeof url, "%s/audio/%s/stream", base, id);
    int rc = http_download_ex(url, sid, wpath, cb, ctx);
    if (rc == -2)
        log_err("download %s: unauthorized/missing", id);
    return rc;
}

static int connect_source(const remote_source_t *source, void **out) {
    audiostream_session_t *session = calloc(1, sizeof *session);
    if (!session)
        return -1;
    snprintf(session->base, sizeof session->base, "%s", source->url);
    if (login(session->base, source->username, source->password, session->session_id,
              sizeof session->session_id)) {
        free(session);
        return -1;
    }
    *out = session;
    return 0;
}

static int list_tracks(void *context, remote_track_t **tracks, int *count) {
    audiostream_session_t *session = context;
    return fetch_tracks(session->base, session->session_id, tracks, count);
}

static int list_playlists(void *context, remote_playlist_t **playlists, int *count) {
    audiostream_session_t *session = context;
    return fetch_playlists(session->base, session->session_id, playlists, count);
}

static int download(void *context, const char *id, const wchar_t *path, remote_progress_cb progress,
                    void *progress_context) {
    audiostream_session_t *session = context;
    return download_track(session->base, session->session_id, id, path, progress, progress_context);
}

static void disconnect(void *session) {
    free(session);
}

const remote_adapter_t audiostream_adapter = {
    .name = "audiostream",
    .requires_username = 1,
    .connect = connect_source,
    .list_tracks = list_tracks,
    .list_playlists = list_playlists,
    .download = download,
    .disconnect = disconnect,
};
