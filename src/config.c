#define _CRT_SECURE_NO_WARNINGS
#include "config.h"
#include "json.h"
#include "log.h"
#include "platform/util.h"
#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <libsecret/secret.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char layout_to_char(int l) {
    if (l == CFG_LAYOUT_A)
        return 'A';
    if (l == CFG_LAYOUT_B)
        return 'B';
    if (l == CFG_LAYOUT_C)
        return 'C';
    return 'D';
}
int char_to_layout(char c) {
    if (c == 'A' || c == 'a')
        return CFG_LAYOUT_A;
    if (c == 'B' || c == 'b')
        return CFG_LAYOUT_B;
    if (c == 'C' || c == 'c')
        return CFG_LAYOUT_C;
    return CFG_LAYOUT_D;
}

char *config_path(void) {
#ifdef _WIN32
    char base[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableA("APPDATA", base, sizeof base);
    if (n == 0 || n >= sizeof base)
        return NULL;
    const char *separator = "\\";
#else
    const char *xdg = getenv("XDG_CONFIG_HOME");
    const char *home = getenv("HOME");
    char base[4096];
    if (xdg && *xdg)
        snprintf(base, sizeof base, "%s", xdg);
    else if (home && *home)
        snprintf(base, sizeof base, "%s/.config", home);
    else
        return NULL;
    const char *separator = "/";
#endif
    size_t need = strlen(base) + 32;
    char *path = malloc(need);
    if (path)
        snprintf(path, need, "%s%saudiosync%sconfig.json", base, separator, separator);
    return path;
}

#ifdef _WIN32
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64enc(const unsigned char *in, size_t len) {
    size_t out = ((len + 2) / 3) * 4;
    char *s = (char *)malloc(out + 1);
    if (!s)
        return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned v = in[i] << 16;
        int pad = 2;
        if (i + 1 < len) {
            v |= in[i + 1] << 8;
            pad--;
        }
        if (i + 2 < len) {
            v |= in[i + 2];
            pad--;
        }
        s[o++] = B64[(v >> 18) & 63];
        s[o++] = B64[(v >> 12) & 63];
        s[o++] = pad >= 2 ? '=' : B64[(v >> 6) & 63];
        s[o++] = pad >= 1 ? '=' : B64[v & 63];
    }
    s[o] = 0;
    return s;
}
static int b64val(char c) {
    if (c >= 'A' && c <= 'Z') {
        return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
        return c - '0' + 52;
    }
    if (c == '+') {
        return 62;
    }
    if (c == '/') {
        return 63;
    }
    return -1;
}
static unsigned char *b64dec(const char *s, size_t *outlen) {
    size_t len = strlen(s), o = 0;
    if (!len || len % 4)
        return NULL;
    unsigned char *out = (unsigned char *)malloc(len);
    if (!out)
        return NULL;
    for (size_t i = 0; i + 3 < len; i += 4) {
        int a = b64val(s[i]), b = b64val(s[i + 1]);
        int c = s[i + 2] == '=' ? 0 : b64val(s[i + 2]);
        int d = s[i + 3] == '=' ? 0 : b64val(s[i + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0)
            break;
        out[o++] = (unsigned char)((a << 2) | (b >> 4));
        if (s[i + 2] != '=')
            out[o++] = (unsigned char)((b << 4) | (c >> 2));
        if (s[i + 3] != '=')
            out[o++] = (unsigned char)((c << 6) | d);
    }
    *outlen = o;
    return out;
}

static char *dpapi_protect(const char *key, const char *plain) {
    (void)key;
    DATA_BLOB in, out;
    in.pbData = (BYTE *)plain;
    in.cbData = (DWORD)strlen(plain) + 1;
    if (!CryptProtectData(&in, L"audiosync", NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out))
        return NULL;
    char *e = b64enc(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return e;
}
static int dpapi_unprotect(const char *enc, char *plain, size_t cap) {
    size_t blen = 0;
    unsigned char *b = b64dec(enc, &blen);
    if (!b)
        return -1;
    DATA_BLOB in, out;
    in.pbData = b;
    in.cbData = (DWORD)blen;
    int ok = CryptUnprotectData(&in, NULL, NULL, NULL, NULL, CRYPTPROTECT_UI_FORBIDDEN, &out);
    free(b);
    if (!ok)
        return -1;
    size_t n = out.cbData < cap - 1 ? out.cbData : cap - 1;
    memcpy(plain, out.pbData, n);
    plain[n] = 0;

    plain[cap - 1] = 0;
    LocalFree(out.pbData);
    return 0;
}

#else
static const SecretSchema password_schema = {"org.audiosync.Password",
                                             SECRET_SCHEMA_NONE,
                                             {{"token", SECRET_SCHEMA_ATTRIBUTE_STRING}, {NULL, 0}},
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL};
static char *dpapi_protect(const char *key, const char *plain) {
    (void)key;
    char *generated = g_uuid_string_random();
    const char *token = generated;
    GError *error = NULL;
    gboolean ok =
        secret_password_store_sync(&password_schema, SECRET_COLLECTION_DEFAULT, "AudioSync device password",
                                   plain ? plain : "", NULL, &error, "token", token, NULL);
    if (!ok) {
        if (error) {
            log_err("keyring: %s", error->message);
            g_error_free(error);
        }
        g_free(generated);
        return NULL;
    }
    char *result = strdup(token);
    g_free(generated);
    return result;
}
static int dpapi_unprotect(const char *token, char *plain, size_t cap) {
    GError *error = NULL;
    char *secret = secret_password_lookup_sync(&password_schema, NULL, &error, "token", token, NULL);
    if (!secret) {
        if (error)
            g_error_free(error);
        return -1;
    }
    snprintf(plain, cap, "%s", secret);
    secret_password_free(secret);
    return 0;
}
#endif
static void json_escape(FILE *f, const char *s) {
    for (; *s; s++) {
        if (*s == '"' || *s == '\\') {
            fputc('\\', f);
            fputc(*s, f);
        } else if (*s == '\n')
            fputs("\\n", f);
        else if (*s == '\r')
            fputs("\\r", f);
        else if (*s == '\t')
            fputs("\\t", f);
        else
            fputc(*s, f);
    }
}

int config_save(const config_t *c) {
    char *path = config_path();
    if (!path)
        return -1;
    wchar_t *wpath = utf8_to_wide(path);
    free(path);
    if (!wpath)
        return -1;
    wchar_t temp[MAX_PATH * 2];
    _snwprintf(temp, sizeof temp / sizeof *temp, L"%s.tmp", wpath);
    wchar_t dir[MAX_PATH * 2];
    _snwprintf(dir, sizeof(dir) / sizeof(dir[0]), L"%s", wpath ? wpath : L"config.json");
    wchar_t *sep = wcsrchr(dir, L'\\');
#ifndef _WIN32
    wchar_t *slash = wcsrchr(dir, L'/');
    if (!sep || (slash && slash > sep))
        sep = slash;
#endif
    if (sep) {
        *sep = 0;
        mkdirs_w(dir);
    }
    FILE *f = _wfopen(temp, L"wb");
#ifndef _WIN32
    if (f)
        fchmod(fileno(f), 0600);
#endif
    if (!f) {
        free(wpath);
        log_err("cannot write config");
        return -1;
    }
    int failed = 0;
    fputs("{\"devices\":[", f);
    for (int i = 0; i < c->n; i++) {
        const device_cfg_t *d = &c->devs[i];
        char *enc = d->password_enc[0] ? str_dup(d->password_enc)
                    : d->password[0]   ? dpapi_protect(d->serial, d->password)
                                       : str_dup("");
        if (!enc) {
            failed = 1;
            break;
        }
        if (i)
            fputc(',', f);
        fprintf(f, "{\"name\":\"");
        json_escape(f, d->name);
        fprintf(f, "\",\"serial\":\"");
        json_escape(f, d->serial);
        fprintf(f, "\",\"label\":\"");
        json_escape(f, d->label);
        fprintf(f, "\",\"target_subdir\":\"");
        json_escape(f, d->target_subdir);
        fprintf(f, "\",\"source_type\":\"");
        json_escape(f, d->source_type);
        fprintf(f, "\",\"base_url\":\"");
        json_escape(f, d->base_url);
        fprintf(f, "\",\"username\":\"");
        json_escape(f, d->username);
        fprintf(f, "\",\"password_enc\":\"");
        json_escape(f, enc);
        fprintf(f, "\",\"local_path\":\"");
        json_escape(f, d->local_path);
        fprintf(f, "\",\"layout\":\"%c\",\"mode\":\"%s\",\"format\":\"%s\"}", layout_to_char(d->layout),
                d->mirror ? "mirror" : "incremental", d->format[0] ? d->format : "keep");
        free(enc);
    }
    fputs("]}", f);
    failed |= ferror(f) != 0;
    failed |= fclose(f) != 0;
    if (!failed && !MoveFileExW(temp, wpath, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        failed = 1;
    if (failed)
        DeleteFileW(temp);
    free(wpath);
    return failed ? -1 : 0;
}

static void jget_str(const char *js, const jtok_t *t, int nt, int obj, const char *key, char *out,
                     size_t cap) {
    int v = json_obj_get(js, t, nt, obj, key);
    if (v < 0 || (size_t)cap == 0) {
        if (cap)
            out[0] = 0;
        return;
    }
    if (t[v].type != J_STR) {

        if (t[v].type == J_NUM) {
            char tmp[64];
            int n = t[v].end - t[v].start;
            if (n > (int)sizeof(tmp) - 1)
                n = (int)sizeof(tmp) - 1;
            memcpy(tmp, js + t[v].start, (size_t)n);
            tmp[n] = 0;
            strncpy(out, tmp, cap - 1);
            out[cap - 1] = 0;
            return;
        }
        out[0] = 0;
        return;
    }
    json_str(js, t, v, out, (unsigned)cap);
}

int config_load(config_t *c) {
    memset(c, 0, sizeof *c);
    char *path = config_path();
    if (!path)
        return -2;
    wchar_t *wpath = utf8_to_wide(path);
    if (!wpath) {
        free(path);
        return -2;
    }
    FILE *f = _wfopen(wpath, L"rb");
    int open_error = errno;
    free(wpath);
    free(path);
    if (!f)
        return open_error == ENOENT ? -1 : -2;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0 || len > 1024 * 1024) {
        fclose(f);
        return -2;
    }
    char *js = (char *)malloc((size_t)len + 1);
    if (!js) {
        fclose(f);
        return -2;
    }
    if (fread(js, 1, (size_t)len, f) != (size_t)len) {
        free(js);
        fclose(f);
        return -2;
    }
    js[len] = 0;
    fclose(f);
    jtok_t *t = (jtok_t *)malloc(sizeof(jtok_t) * 2048);
    if (!t) {
        free(js);
        return -2;
    }
    int nt = json_parse(js, (unsigned)len, t, 2048);
    if (nt <= 0 || t[0].type != J_OBJ) {
        free(js);
        free(t);
        return -2;
    }
    int devs = json_obj_get(js, t, nt, 0, "devices");
    if (devs < 0 || t[devs].type != J_ARR) {
        free(js);
        free(t);
        return -2;
    }
    int n = json_arr_len(t, devs);
    c->devs = (device_cfg_t *)calloc((size_t)(n > 0 ? n : 1), sizeof(device_cfg_t));
    if (!c->devs) {
        free(js);
        free(t);
        return -2;
    }
    for (int i = 0; i < n; i++) {
        int o = json_arr_at(t, nt, devs, i);
        device_cfg_t *d = &c->devs[c->n];
        if (o < 0 || t[o].type != J_OBJ) {
            config_free(c);
            free(js);
            free(t);
            return -2;
        }
        jget_str(js, t, nt, o, "name", d->name, sizeof d->name);
        jget_str(js, t, nt, o, "serial", d->serial, sizeof d->serial);
        jget_str(js, t, nt, o, "label", d->label, sizeof d->label);
        jget_str(js, t, nt, o, "target_subdir", d->target_subdir, sizeof d->target_subdir);
        if (!d->target_subdir[0])
            strcpy(d->target_subdir, "Music");
        jget_str(js, t, nt, o, "source_type", d->source_type, sizeof d->source_type);
        if (!strcmp(d->source_type, "remote"))
            strcpy(d->source_type, "audiostream");
        jget_str(js, t, nt, o, "base_url", d->base_url, sizeof d->base_url);
        jget_str(js, t, nt, o, "username", d->username, sizeof d->username);
        jget_str(js, t, nt, o, "local_path", d->local_path, sizeof d->local_path);
        jget_str(js, t, nt, o, "password_enc", d->password_enc, sizeof d->password_enc);
        if (d->password_enc[0]) {
            if (dpapi_unprotect(d->password_enc, d->password, sizeof d->password) != 0) {
                log_warn("device '%s': password store unavailable; retaining stored credential", d->name);
                d->password[0] = 0;
            }
        }
        char lay[8] = {0}, mode[16] = {0};
        jget_str(js, t, nt, o, "layout", lay, sizeof lay);
        jget_str(js, t, nt, o, "mode", mode, sizeof mode);
        jget_str(js, t, nt, o, "format", d->format, sizeof d->format);
        if (!d->format[0])
            strcpy(d->format, "keep");
        d->layout = char_to_layout(lay[0] ? lay[0] : 'D');
        d->mirror = (strcmp(mode, "mirror") == 0);
        c->n++;
    }
    free(js);
    free(t);
    return 0;
}

void config_free(config_t *c) {
    if (c->devs) {
        for (int i = 0; i < c->n; i++)
            memset(c->devs[i].password, 0, sizeof c->devs[i].password);
        free(c->devs);
        c->devs = NULL;
        c->n = 0;
    }
}
