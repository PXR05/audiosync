#include "sync.h"
#include "adapters/adapter.h"
#include "convert.h"
#include "json.h"
#include "log.h"
#include "organizer.h"
#include "progress.h"
#include "platform/util.h"
#include "platform/compat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MANIFEST_NAME ".audiosync-manifest.json"

static wchar_t *join_w(const wchar_t *root, const char *rel_utf8) {
    wchar_t *wrel = utf8_to_wide(rel_utf8);
    if (!wrel)
        return NULL;
    for (wchar_t *p = wrel; *p; p++)
        if (*p == L'/')
            *p = L'\\';
    size_t need = wcslen(root) + 1 + wcslen(wrel) + 1;
    wchar_t *out = (wchar_t *)malloc(need * sizeof(wchar_t));
    if (out) {
        wcscpy(out, root);
        if (out[wcslen(out) - 1] != L'\\')
            wcscat(out, L"\\");
        wcscat(out, wrel);
    }
    free(wrel);
    return out;
}

static void ensure_parent_w(const wchar_t *wpath) {
    wchar_t dir[MAX_PATH * 2];
    wcsncpy(dir, wpath, sizeof(dir) / sizeof(dir[0]) - 1);
    dir[sizeof(dir) / sizeof(dir[0]) - 1] = 0;
    wchar_t *sep = wcsrchr(dir, L'\\');
    if (sep) {
        *sep = 0;
        mkdirs_w(dir);
    }
}

typedef struct {
    char path[512];
    long long size;
    char id[64];
} man_entry_t;
typedef struct {
    man_entry_t *e;
    int n, cap, error;
} manifest_t;

static void man_add(manifest_t *m, const char *path, long long size, const char *id) {
    if (m->n >= m->cap) {
        int cap = m->cap ? m->cap * 2 : 256;
        man_entry_t *ne = (man_entry_t *)realloc(m->e, (size_t)cap * sizeof *ne);
        if (!ne) {
            m->error = 1;
            return;
        }
        m->e = ne;
        m->cap = cap;
    }
    snprintf(m->e[m->n].path, sizeof m->e[m->n].path, "%s", path);
    m->e[m->n].size = size;
    snprintf(m->e[m->n].id, sizeof m->e[m->n].id, "%s", id ? id : "");
    m->n++;
}

static int man_has(const manifest_t *m, const char *path) {
    for (int i = 0; i < m->n; i++)
        if (text_equal_ci(m->e[i].path, path))
            return i;
    return -1;
}

static void man_load(const wchar_t *wroot, manifest_t *m) {
    memset(m, 0, sizeof *m);
    wchar_t *wman = join_w(wroot, MANIFEST_NAME);
    if (!wman)
        return;
    FILE *f = _wfopen(wman, L"rb");
    free(wman);
    if (!f)
        return;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 8 * 1024 * 1024) {
        fclose(f);
        return;
    }
    char *js = (char *)malloc((size_t)len + 1);
    if (!js) {
        fclose(f);
        return;
    }
    if (fread(js, 1, (size_t)len, f) != (size_t)len) {
        free(js);
        fclose(f);
        return;
    }
    js[len] = 0;
    fclose(f);
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * 16384);
    if (!t) {
        free(js);
        return;
    }
    int nt = json_parse(js, (unsigned)len, t, 16384);
    if (nt > 0) {
        int files = json_obj_get(js, t, nt, 0, "files");
        if (files >= 0) {
            int fc = json_arr_len(t, files);
            for (int i = 0; i < fc; i++) {
                int o = json_arr_at(t, nt, files, i);
                int pv = json_obj_get(js, t, nt, o, "path");
                int sv = json_obj_get(js, t, nt, o, "size");
                int iv = json_obj_get(js, t, nt, o, "id");
                char p[512] = {0}, id[64] = {0};
                if (pv >= 0)
                    json_str(js, t, pv, p, sizeof p);
                if (iv >= 0)
                    json_str(js, t, iv, id, sizeof id);
                if (p[0])
                    man_add(m, p, json_int(js, t, sv), id);
            }
        }
    }
    free(t);
    free(js);
}

static void man_save(const wchar_t *wroot, const manifest_t *m) {
    wchar_t *wman = join_w(wroot, MANIFEST_NAME);
    if (!wman)
        return;
    wchar_t tmp[1024];
    _snwprintf(tmp, 1024, L"%s.audiosync-part", wman);
    FILE *f = _wfopen(tmp, L"wb");
    if (!f) {
        free(wman);
        return;
    }
    fputs("{\"files\":[", f);
    for (int i = 0; i < m->n; i++) {
        if (i)
            fputc(',', f);
        fprintf(f, "{\"path\":\"");
        for (const char *s = m->e[i].path; *s; s++) {
            if (*s == '"' || *s == '\\')
                fputc('\\', f);
            fputc(*s, f);
        }
        fprintf(f, "\",\"size\":%lld,\"id\":\"%s\"}", m->e[i].size, m->e[i].id);
    }
    fputs("]}", f);
    int bad = ferror(f);
    if (fclose(f) != 0)
        bad = 1;
    if (bad || !MoveFileExW(tmp, wman, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        log_warn("Could not save sync manifest");
        DeleteFileW(tmp);
    }
    free(wman);
}

typedef void (*file_cb)(const wchar_t *wfull, const char *rel, void *ctx);

static int walk_rec(const wchar_t *wbase, const wchar_t *wsub, file_cb cb, void *ctx) {
    wchar_t pat[MAX_PATH * 2];
    if (wcslen(wbase) + wcslen(wsub) + 4 >= sizeof(pat) / sizeof(pat[0]))
        return -1;
    _snwprintf(pat, sizeof(pat) / sizeof(pat[0]), L"%s%s*.*", wbase, wsub);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return GetLastError() == ERROR_FILE_NOT_FOUND ? 0 : -1;
    int rc = 0;
    do {
        if (!wcscmp(fd.cFileName, L".") || !wcscmp(fd.cFileName, L".."))
            continue;
        wchar_t rel2[MAX_PATH * 2], full[MAX_PATH * 2];
        if (wcslen(wbase) + wcslen(wsub) + wcslen(fd.cFileName) + 2 >= MAX_PATH * 2) {
            rc = -1;
            continue;
        }
        _snwprintf(rel2, sizeof(rel2) / sizeof(rel2[0]), L"%s%s", wsub, fd.cFileName);
        _snwprintf(full, sizeof(full) / sizeof(full[0]), L"%s%s", wbase, rel2);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                rc = -1;
                continue;
            }
            wcscat(rel2, L"\\");
            if (walk_rec(wbase, rel2, cb, ctx) != 0)
                rc = -1;
        } else {
            char *rel = wide_to_utf8(rel2);
            if (rel) {
                for (char *p = rel; *p; p++)
                    if (*p == '\\')
                        *p = '/';
                cb(full, rel, ctx);
                free(rel);
            } else
                rc = -1;
        }
    } while (FindNextFileW(h, &fd));
    if (GetLastError() != ERROR_NO_MORE_FILES)
        rc = -1;
    FindClose(h);
    return rc;
}

typedef struct {
    wchar_t *wfull;
    char *rel;
} localfile_t;
typedef struct {
    localfile_t *f;
    int n, cap, error;
} localfile_list_t;

static void local_collect_cb(const wchar_t *wfull, const char *rel, void *ctx) {
    localfile_list_t *l = (localfile_list_t *)ctx;
    if (l->n >= l->cap) {
        int cap = l->cap ? l->cap * 2 : 256;
        localfile_t *nf = (localfile_t *)realloc(l->f, (size_t)cap * sizeof *nf);
        if (!nf) {
            l->error = 1;
            return;
        }
        l->f = nf;
        l->cap = cap;
    }
    size_t wl = wcslen(wfull) + 1;
    l->f[l->n].wfull = (wchar_t *)malloc(wl * sizeof(wchar_t));
    if (!l->f[l->n].wfull) {
        l->error = 1;
        return;
    }
    wcscpy(l->f[l->n].wfull, wfull);
    l->f[l->n].rel = _strdup(rel);
    if (!l->f[l->n].rel) {
        free(l->f[l->n].wfull);
        l->error = 1;
        return;
    }
    l->n++;
}

static void local_out_rel(const char *rel, const char *fmt, char *out, size_t cap) {
    snprintf(out, cap, "%s", rel);
    if (!convert_needed(fmt, file_ext(rel)))
        return;
    char *dot = strrchr(out, '.');
    char *sep = strrchr(out, '/');
    if (dot && (!sep || dot > sep))
        snprintf(dot, cap - (size_t)(dot - out), ".%s", convert_ext(fmt));
    else {
        size_t n = strlen(out);
        snprintf(out + n, cap - n, ".%s", convert_ext(fmt));
    }
}

static DWORD CALLBACK copy_prog(LARGE_INTEGER total, LARGE_INTEGER done, LARGE_INTEGER sn, LARGE_INTEGER sb,
                                DWORD n, DWORD reason, HANDLE hs, HANDLE hd, LPVOID d) {
    (void)sn;
    (void)sb;
    (void)n;
    (void)reason;
    (void)hs;
    (void)hd;
    (void)d;
    static DWORD last = 0;
    DWORD now = GetTickCount();
    if (now - last > 120) {
        last = now;
        progress_bytes((unsigned long long)done.QuadPart, (unsigned long long)total.QuadPart);
    }
    return PROGRESS_CONTINUE;
}

static void free_local_list(localfile_list_t *l) {
    for (int i = 0; i < l->n; i++) {
        free(l->f[i].wfull);
        free(l->f[i].rel);
    }
    free(l->f);
    l->f = NULL;
    l->n = l->cap = 0;
}

typedef struct {
    const wchar_t *wroot;
    manifest_t *want;
    int deleted;
} sweep_ctx_t;

static void sweep_cb(const wchar_t *wfull, const char *rel, void *ctx) {
    sweep_ctx_t *c = (sweep_ctx_t *)ctx;
    if (strcmp(rel, MANIFEST_NAME) == 0)
        return;
    size_t n = strlen(rel);
    if (n > 5 && strcmp(rel + n - 5, ".part") == 0)
        return;

    if (man_has(c->want, rel) < 0) {
        if (DeleteFileW(wfull)) {
            c->deleted++;
        } else
            log_warn("delete failed: %s", rel);
    }
}

static int sync_local_to(const char *local_utf8, const wchar_t *wroot, int mirror, const char *fmt) {
    wchar_t *wsrc = utf8_to_wide(local_utf8);
    if (!wsrc || !*wsrc) {
        free(wsrc);
        return -1;
    }
    DWORD attr = GetFileAttributesW(wsrc);
    if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) {
        free(wsrc);
        log_err("Source folder is unavailable");
        return -1;
    }
    wchar_t srcabs[1024], dstabs[1024];
    DWORD srclen = GetFullPathNameW(wsrc, 1024, srcabs, NULL),
          dstlen = GetFullPathNameW(wroot, 1024, dstabs, NULL);
    if (!srclen || srclen >= 1024 || !dstlen || dstlen >= 1024) {
        free(wsrc);
        return -1;
    }
    size_t sl = wcslen(srcabs), dl = wcslen(dstabs);
    while (sl && (srcabs[sl - 1] == L'\\' || srcabs[sl - 1] == L'/'))
        srcabs[--sl] = 0;
    while (dl && (dstabs[dl - 1] == L'\\' || dstabs[dl - 1] == L'/'))
        dstabs[--dl] = 0;
    if ((!_wcsnicmp(srcabs, dstabs, sl) && (!dstabs[sl] || dstabs[sl] == L'\\' || dstabs[sl] == L'/')) ||
        (!_wcsnicmp(srcabs, dstabs, dl) && (!srcabs[dl] || srcabs[dl] == L'\\' || srcabs[dl] == L'/'))) {
        free(wsrc);
        log_err("Source and destination folders must not overlap");
        return -1;
    }
    size_t L = wcslen(wsrc);
    wchar_t *wbase = (wchar_t *)malloc((L + 4) * sizeof(wchar_t));
    if (!wbase) {
        free(wsrc);
        return -1;
    }
    wcscpy(wbase, wsrc);
    if (wbase[L - 1] != L'\\' && wbase[L - 1] != L'/')
        wcscat(wbase, L"\\");
    free(wsrc);
    mkdirs_w(wroot);
    progress_stage("Scanning local library...");
    localfile_list_t list;
    memset(&list, 0, sizeof list);
    int scan_rc = walk_rec(wbase, L"", local_collect_cb, &list);
    free(wbase);
    if (scan_rc != 0 || list.error) {
        free_local_list(&list);
        log_err("Incomplete source scan; sync stopped");
        return -1;
    }
    manifest_t have;
    man_load(wroot, &have);
    manifest_t want;
    memset(&want, 0, sizeof want);
    char ffmpeg[MAX_PATH * 2] = {0};
    progress_begin(list.n);
    int fresh = 0, skipped = 0, failed = 0;
    char orel[576];
    for (int i = 0; i < list.n; i++) {
        local_out_rel(list.f[i].rel, fmt, orel, sizeof orel);
        progress_file(i + 1, list.n, orel);
        wchar_t *wdst = join_w(wroot, orel);
        if (!wdst) {
            failed++;
            continue;
        }
        unsigned long long ss = 0;
        file_size_w(list.f[i].wfull, &ss);
        unsigned long long ds = 0;
        if (file_size_w(wdst, &ds) == 0) {
            skipped++;
            man_add(&want, orel, (long long)ds, "");
            free(wdst);
            continue;
        }
        ensure_parent_w(wdst);
        int ok = 0;
        if (convert_needed(fmt, file_ext(list.f[i].rel))) {
            progress_convert(orel);
            wchar_t wpart[MAX_PATH * 2];
            _snwprintf(wpart, sizeof(wpart) / sizeof(wpart[0]), L"%s.conv.%hs", wdst, convert_ext(fmt));
            if ((ffmpeg[0] || convert_find_ffmpeg(ffmpeg, sizeof ffmpeg) == 0) &&
                convert_run(ffmpeg, list.f[i].wfull, wpart, fmt) == 0)
                ok = MoveFileExW(wpart, wdst, 0);
            if (!ok)
                DeleteFileW(wpart);
        } else {
            progress_bytes(0, ss);
            wchar_t wpart[1024];
            _snwprintf(wpart, 1024, L"%s.audiosync-part", wdst);
            ok = CopyFileExW(list.f[i].wfull, wpart, copy_prog, NULL, NULL, 0);
            if (ok)
                ok = MoveFileExW(wpart, wdst, 0);
            if (!ok)
                DeleteFileW(wpart);
            progress_bytes(ss, ss);
        }
        if (ok) {
            fresh++;
            man_add(&want, orel, (long long)ss, "");
        } else {
            failed++;
            log_warn("copy failed: %s", orel);
        }
        free(wdst);
        if ((i + 1) % 50 == 0)
            man_save(wroot, &want);
    }
    free_local_list(&list);
    free(have.e);
    if (mirror && failed == 0 && !want.error) {
        wchar_t wb[MAX_PATH * 2];
        _snwprintf(wb, sizeof(wb) / sizeof(wb[0]), L"%s\\", wroot);
        wchar_t *wbr = _wcsdup(wb);
        if (wbr) {
            sweep_ctx_t s;
            s.wroot = wroot;
            s.want = &want;
            s.deleted = 0;
            walk_rec(wbr, L"", sweep_cb, &s);
            free(wbr);
            log_info("local: mirror deleted %d", s.deleted);
        }
    }
    if (want.error)
        failed++;
    man_save(wroot, &want);
    progress_end(fresh, skipped, failed);
    free(want.e);
    return failed > 0 ? -1 : 0;
}

typedef struct {
    char rel[1024];
    char id[64];
    long long size;
    char sext[16];
} plan_t;
typedef struct {
    plan_t *p;
    int n, cap, error;
} planlist_t;

static void plan_add(planlist_t *l, const char *rel, const char *id, long long size, const char *sext) {
    for (int i = 0; i < l->n; i++)
        if (strcmp(l->p[i].rel, rel) == 0)
            return;
    if (l->n >= l->cap) {
        int cap = l->cap ? l->cap * 2 : 512;
        plan_t *np = (plan_t *)realloc(l->p, (size_t)cap * sizeof *np);
        if (!np) {
            l->error = 1;
            return;
        }
        l->p = np;
        l->cap = cap;
    }
    snprintf(l->p[l->n].rel, sizeof l->p[l->n].rel, "%s", rel);
    snprintf(l->p[l->n].id, sizeof l->p[l->n].id, "%s", id);
    l->p[l->n].size = size;
    snprintf(l->p[l->n].sext, sizeof l->p[l->n].sext, "%s", sext ? sext : "");
    l->n++;
}

static void dl_prog(unsigned long long done, unsigned long long total, void *ctx) {
    (void)ctx;
    progress_bytes(done, total);
}

static int in_playlist(const remote_playlist_t *playlists, int count, const char *id) {
    for (int i = 0; i < count; i++)
        for (int k = 0; k < playlists[i].count; k++)
            if (strcmp(playlists[i].items[k].track.id, id) == 0)
                return 1;
    return 0;
}

static int cmp_orphan(const void *a, const void *b) {
    const remote_track_t *ta = a, *tb = b;
    int c = strcmp(ta->album, tb->album);
    if (c)
        return c;
    c = strcmp(ta->uploaded_at, tb->uploaded_at);
    if (c)
        return c;
    return strcmp(ta->title, tb->title);
}

static int download_complete(const wchar_t *path, long long expected) {
    unsigned long long size = 0;
    return file_size_w(path, &size) == 0 && (expected <= 0 || size == (unsigned long long)expected);
}
static void clear_run_cache(const char *dir) {
    wchar_t *root = utf8_to_wide(dir);
    if (!root)
        return;
    wchar_t *pattern = join_w(root, "*");
    WIN32_FIND_DATAW fd;
    HANDLE h = pattern ? FindFirstFileW(pattern, &fd) : INVALID_HANDLE_VALUE;
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))) {
                wchar_t file[1024];
                _snwprintf(file, 1024, L"%s\\%s", root, fd.cFileName);
                DeleteFileW(file);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    RemoveDirectoryW(root);
    free(pattern);
    free(root);
}

static int sync_remote_to(const device_cfg_t *dev, const wchar_t *wroot, const remote_adapter_t *adapter) {
    char fmt[16];
    convert_normalize(dev->format, fmt, sizeof fmt);
    const char *force_ext = convert_ext(fmt);
    char ffmpeg[MAX_PATH * 2] = {0};
    progress_stage("Connecting to %s...", adapter->name);
    remote_source_t source = {dev->base_url, dev->username, dev->password};
    void *session = NULL;
    if (adapter->connect(&source, &session) != 0) {
        log_err("%s: connection failed for %s", adapter->name, dev->username);
        return -1;
    }
    progress_stage("Fetching playlists...");
    remote_playlist_t *pls = NULL;
    int npl = 0;
    if (adapter->list_playlists(session, &pls, &npl) != 0) {
        adapter->disconnect(session);
        return -1;
    }
    progress_stage("Fetching track list...");
    remote_track_t *all = NULL;
    int nall = 0;
    if (adapter->list_tracks(session, &all, &nall) != 0) {
        remote_playlists_free(pls, npl);
        adapter->disconnect(session);
        return -1;
    }

    planlist_t plan;
    memset(&plan, 0, sizeof plan);
    char rel[576];
    for (int i = 0; i < npl; i++) {
        int w = num_width(pls[i].count > 0 ? pls[i].count : 1);
        for (int k = 0; k < pls[i].count; k++) {
            remote_track_t *a = &pls[i].items[k].track;
            if (!a->id[0])
                continue;
            const char *folder = pls[i].name[0] ? pls[i].name : (a->album[0] ? a->album : "Unknown Album");
            const char *ttl = a->title[0] ? a->title : a->filename;
            const char *sext = file_ext(a->filename);
            const char *use_ext = *force_ext ? force_ext : sext;
            if (dev->layout == CFG_LAYOUT_D || dev->layout == CFG_LAYOUT_C) {

                layout_d_path(rel, sizeof rel, folder, k + 1, w, a->artist, ttl, use_ext);
            } else if (dev->layout == CFG_LAYOUT_A) {
                layout_a_path(rel, sizeof rel, a->artist, a->album, ttl, use_ext);
            } else {
                layout_b_path(rel, sizeof rel, a->artist, ttl, a->id, use_ext);
            }
            plan_add(&plan, rel, a->id, a->size, sext);
        }
    }

    {
        remote_track_t *orp = malloc((size_t)(nall > 0 ? nall : 1) * sizeof *orp);
        int no = 0;
        if (orp) {
            for (int i = 0; i < nall; i++)
                if (!in_playlist(pls, npl, all[i].id))
                    orp[no++] = all[i];
            qsort(orp, (size_t)no, sizeof *orp, cmp_orphan);
            int idx = 0;
            while (idx < no) {
                int j = idx;
                const char *alb = orp[idx].album[0] ? orp[idx].album : "Unknown Album";
                while (j < no && strcmp(orp[j].album, orp[idx].album) == 0)
                    j++;
                int w = num_width(j - idx);
                for (int k = idx; k < j; k++) {
                    const char *sext = file_ext(orp[k].filename);
                    const char *title = orp[k].title[0] ? orp[k].title : orp[k].filename;
                    const char *ext = *force_ext ? force_ext : sext;
                    if (dev->layout == CFG_LAYOUT_A)
                        layout_a_path(rel, sizeof rel, orp[k].artist, alb, title, ext);
                    else if (dev->layout == CFG_LAYOUT_B)
                        layout_b_path(rel, sizeof rel, orp[k].artist, title, orp[k].id, ext);
                    else
                        layout_d_path(rel, sizeof rel, alb, k - idx + 1, w, orp[k].artist, title, ext);
                    plan_add(&plan, rel, orp[k].id, orp[k].size, sext);
                }
                idx = j;
            }
            free(orp);
        } else
            plan.error = 1;
    }

    mkdirs_w(wroot);

    char tmpdir[MAX_PATH] = {0};
    GetTempPathA(sizeof tmpdir, tmpdir);
    size_t tmp_len = strlen(tmpdir);
    snprintf(tmpdir + tmp_len, sizeof tmpdir - tmp_len, "audiosync-%lu-%llu", GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    wchar_t *wtmp = utf8_to_wide(tmpdir);
    if (wtmp) {
        mkdirs_w(wtmp);
        free(wtmp);
    }

    manifest_t have;
    man_load(wroot, &have);
    manifest_t want;
    memset(&want, 0, sizeof want);
    progress_begin(plan.n);
    int dl_ok = 0, dl_up = 0, dl_fail = plan.error ? 1 : 0;
    for (int i = 0; i < plan.n; i++) {
        plan_t *P = &plan.p[i];
        progress_file(i + 1, plan.n, P->rel);
        if (!P->id[0] || strspn(P->id, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") !=
                             strlen(P->id)) {
            dl_fail++;
            log_err("Unsupported track identifier");
            continue;
        }
        int conv = convert_needed(fmt, P->sext);
        wchar_t *wdst = join_w(wroot, P->rel);
        if (!wdst) {
            dl_fail++;
            continue;
        }
        unsigned long long ds = 0;
        if (file_size_w(wdst, &ds) == 0) {
            dl_up++;
            man_add(&want, P->rel, (long long)ds, P->id);
            free(wdst);
            continue;
        }
        if (conv && !ffmpeg[0] && convert_find_ffmpeg(ffmpeg, sizeof ffmpeg) != 0) {
            dl_fail++;
            log_err("Conversion requires ffmpeg");
            free(wdst);
            continue;
        }
        ensure_parent_w(wdst);
        wchar_t wpart[MAX_PATH * 2];
        if (conv)
            _snwprintf(wpart, sizeof(wpart) / sizeof(wpart[0]), L"%s.conv.%hs", wdst, convert_ext(fmt));
        else
            _snwprintf(wpart, sizeof(wpart) / sizeof(wpart[0]), L"%s.audiosync-part", wdst);
        int done = 0;
        if (!conv) {
            char cpath[MAX_PATH * 2];
            snprintf(cpath, sizeof cpath, "%s\\%s.cache", tmpdir, P->id);
            wchar_t *wc = utf8_to_wide(cpath);
            unsigned long long cs = 0;
            if (wc && file_size_w(wc, &cs) == 0 && (long long)cs == P->size && P->size > 0) {
                if (CopyFileW(wc, wpart, FALSE) && MoveFileExW(wpart, wdst, 0))
                    done = 1;
                if (!done)
                    DeleteFileW(wpart);
            } else {
                progress_bytes(0, (unsigned long long)(P->size > 0 ? P->size : 0));
                if (adapter->download(session, P->id, wpart, dl_prog, NULL) == 0 &&
                    download_complete(wpart, P->size)) {
                    if (MoveFileExW(wpart, wdst, 0)) {
                        done = 1;
                        if (wc)
                            CopyFileW(wdst, wc, FALSE);
                    } else
                        DeleteFileW(wpart);
                } else
                    DeleteFileW(wpart);
            }
            free(wc);
        } else {
            char spath[MAX_PATH * 2], cpath[MAX_PATH * 2];
            snprintf(spath, sizeof spath, "%s\\%s.src.cache", tmpdir, P->id);
            snprintf(cpath, sizeof cpath, "%s\\%s.%s.cache", tmpdir, P->id, fmt);
            wchar_t *ws = utf8_to_wide(spath), *wc = utf8_to_wide(cpath);
            unsigned long long cs = 0;
            if (wc && file_size_w(wc, &cs) == 0 && cs > 0) {
                if (CopyFileW(wc, wpart, FALSE) && MoveFileExW(wpart, wdst, 0))
                    done = 1;
                if (!done)
                    DeleteFileW(wpart);
            } else {
                int have_src = (ws && file_size_w(ws, &cs) == 0 && (long long)cs == P->size && P->size > 0);
                if (!have_src) {
                    if (ws) {
                        progress_bytes(0, (unsigned long long)(P->size > 0 ? P->size : 0));
                        if (adapter->download(session, P->id, ws, dl_prog, NULL) != 0 ||
                            !download_complete(ws, P->size)) {
                            DeleteFileW(ws);
                            free(ws);
                            ws = NULL;
                        }
                    }
                }
                if (ws && file_size_w(ws, &cs) == 0) {
                    progress_convert(P->rel);
                    if (convert_run(ffmpeg, ws, wpart, fmt) == 0) {
                        if (MoveFileExW(wpart, wdst, 0)) {
                            done = 1;
                            if (wc)
                                CopyFileW(wdst, wc, FALSE);
                        } else
                            DeleteFileW(wpart);
                    } else
                        DeleteFileW(wpart);
                }
            }
            free(ws);
            free(wc);
        }
        if (done) {
            dl_ok++;
            man_add(&want, P->rel, P->size, P->id);
        } else {
            dl_fail++;
            log_warn("sync failed: %s", P->rel);
        }
        free(wdst);
        if ((i + 1) % 50 == 0)
            man_save(wroot, &want);
    }
    free(have.e);

    if (dev->layout == CFG_LAYOUT_C) {

        for (int i = 0; i < npl; i++) {
            char mrel[512];
            char pn[256];
            snprintf(pn, sizeof pn, "%s", pls[i].name);
            sanitize_component(pn);
            snprintf(mrel, sizeof mrel, "Playlists/%s.m3u8", pn);
            wchar_t *wm = join_w(wroot, mrel);
            if (!wm)
                continue;
            ensure_parent_w(wm);
            unsigned long long playlist_size = 0;
            if (file_size_w(wm, &playlist_size) == 0) {
                man_add(&want, mrel, (long long)playlist_size, "");
                free(wm);
                continue;
            }
            FILE *f = _wfopen(wm, L"wb");
            if (f) {
                fputs("#EXTM3U\n", f);
                int w = num_width(pls[i].count > 0 ? pls[i].count : 1);
                for (int k = 0; k < pls[i].count; k++) {
                    remote_track_t *a = &pls[i].items[k].track;

                    for (int q = 0; q < plan.n; q++) {
                        if (strcmp(plan.p[q].id, a->id) == 0) {
                            fprintf(f, "../%s\n", plan.p[q].rel);
                            break;
                        }
                    }
                    (void)w;
                }
                fclose(f);
                man_add(&want, mrel, 0, "");
            }
            free(wm);
        }
    }

    if (dev->mirror && dl_fail == 0 && !want.error) {
        wchar_t wb[MAX_PATH * 2];
        _snwprintf(wb, sizeof(wb) / sizeof(wb[0]), L"%s\\", wroot);
        wchar_t *wbr = _wcsdup(wb);
        if (wbr) {
            sweep_ctx_t sc;
            sc.wroot = wroot;
            sc.want = &want;
            sc.deleted = 0;
            walk_rec(wbr, L"", sweep_cb, &sc);
            log_info("remote: mirror deleted %d", sc.deleted);
            free(wbr);
        }
    }
    if (want.error)
        dl_fail++;
    man_save(wroot, &want);
    progress_end(dl_ok, dl_up, dl_fail);
    free(want.e);
    free(plan.p);
    clear_run_cache(tmpdir);
    remote_tracks_free(all);
    remote_playlists_free(pls, npl);
    adapter->disconnect(session);
    return dl_fail > 0 ? -1 : 0;
}

int sync_target_valid(const char *path) {
    if (!path || !*path || strlen(path) >= 128 || strpbrk(path, ":*?\"<>|"))
        return 0;
    if (*path == '/' || *path == '\\')
        return 0;
    const char *start = path;
    for (const char *p = path;; p++) {
        if (!*p || *p == '/' || *p == '\\') {
            size_t n = (size_t)(p - start);
            if (!n || start[n - 1] == '.' || start[n - 1] == ' ')
                return 0;
            if (!*p)
                break;
            start = p + 1;
        } else if ((unsigned char)*p < 32)
            return 0;
    }
    return 1;
}

int sync_device(const device_cfg_t *dev, const drive_info_t *drives, int ndrives) {
    const drive_info_t *d = devices_find_by_serial(drives, ndrives, dev->serial);
    if (!d)
        return 1;
    if (!sync_target_valid(dev->target_subdir)) {
        log_err("Invalid target folder");
        return -1;
    }
    wchar_t wroot[MAX_PATH * 2];
    wchar_t rel[256];
    MultiByteToWideChar(CP_UTF8, 0, dev->target_subdir, -1, rel, 256);
#ifdef _WIN32
    for (wchar_t *p = rel; *p; p++)
        if (*p == L'/')
            *p = L'\\';
    _snwprintf(wroot, sizeof(wroot) / sizeof(wroot[0]), L"%c:\\%s", d->letter, rel);
    log_info("sync '%s' -> %c:\\%s [%s/%s/%s]", dev->name, d->letter, dev->target_subdir, dev->source_type,
             dev->mirror ? "mirror" : "incremental", dev->format[0] ? dev->format : "keep");
#else
    _snwprintf(wroot, sizeof(wroot) / sizeof(wroot[0]), L"%hs/%ls", d->mount, rel);
    log_info("sync '%s' -> %s/%s [%s/%s/%s]", dev->name, d->mount, dev->target_subdir, dev->source_type,
             dev->mirror ? "mirror" : "incremental", dev->format[0] ? dev->format : "keep");
#endif
    if (strcmp(dev->source_type, "local") == 0) {
        if (!dev->local_path[0]) {
            log_err("no local_path");
            return -1;
        }
        char fmt[16];
        convert_normalize(dev->format, fmt, sizeof fmt);
        return sync_local_to(dev->local_path, wroot, dev->mirror, fmt);
    }
    const remote_adapter_t *adapter = remote_adapter_find(dev->source_type);
    if (!adapter) {
        log_err("Unknown source adapter: %s", dev->source_type);
        return -1;
    }
    return sync_remote_to(dev, wroot, adapter);
}

int sync_all(const char *only) {
    config_t c;
    int loaded = config_load(&c);
    if (loaded != 0 && loaded != -1) {
        log_err("Cannot read configuration");
        config_free(&c);
        return -1;
    }
    if (loaded == -1 || c.n == 0) {
        log_warn("no configured devices (run 'audiosync add NAME --serial ID --local PATH')");
        config_free(&c);
        return 99;
    }
    drive_info_t ds[26];
    int n = devices_list(ds, 26);
    int matched = 0, failed = 0;
    for (int i = 0; i < c.n; i++) {
        if (only && *only && !text_equal_ci(c.devs[i].name, only) && !text_equal_ci(c.devs[i].label, only) &&
            !text_equal_ci(c.devs[i].serial, only))
            continue;
        int rc = sync_device(&c.devs[i], ds, n);
        if (rc == 1)
            log_info("device '%s' not plugged, skipping", c.devs[i].name);
        else {
            matched++;
            if (rc != 0)
                failed++;
        }
    }
    config_free(&c);
    if (matched == 0)
        return 99;
    return failed ? -1 : 0;
}
