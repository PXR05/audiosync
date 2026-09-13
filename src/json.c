#include "json.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

static int alloc_tok(jtok_t *toks, unsigned cap, unsigned *n) {
    if (*n >= cap)
        return -1;
    unsigned i = (unsigned)(*n)++;
    toks[i].type = J_NULL;
    toks[i].start = toks[i].end = -1;
    toks[i].size = 0;
    toks[i].parent = -1;
    return (int)i;
}

int json_parse(const char *js, unsigned len, jtok_t *toks, unsigned cap) {
    unsigned n = 0;
    int parent = -1, i = 0;
    while ((unsigned)i < len) {
        char c = js[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            i++;
            continue;
        }
        if (c == '{' || c == '[') {
            int t = alloc_tok(toks, cap, &n);
            if (t < 0)
                return -1;
            toks[t].type = (c == '{') ? J_OBJ : J_ARR;
            toks[t].start = i;
            toks[t].parent = parent;
            if (parent >= 0)
                toks[parent].size++;
            parent = t;
            i++;
            continue;
        }
        if (c == '}' || c == ']') {
            jtype_t want = (c == '}') ? J_OBJ : J_ARR;
            if (parent < 0 || toks[parent].type != want)
                return -2;
            toks[parent].end = i + 1;
            parent = toks[parent].parent;
            i++;
            continue;
        }
        if (c == '"') {
            int s = i + 1, j = s;
            while ((unsigned)j < len) {
                if (js[j] == '\\') {
                    j += 2;
                    continue;
                }
                if (js[j] == '"')
                    break;
                j++;
            }
            if ((unsigned)j >= len)
                return -2;
            int t = alloc_tok(toks, cap, &n);
            if (t < 0)
                return -1;
            toks[t].type = J_STR;
            toks[t].start = s;
            toks[t].end = j;
            toks[t].parent = parent;
            if (parent >= 0)
                toks[parent].size++;
            i = j + 1;
            continue;
        }
        if (c == ':' || c == ',') {
            i++;
            continue;
        }

        {
            int s = i;
            while ((unsigned)i < len && js[i] != ',' && js[i] != '}' && js[i] != ']' && js[i] != ' ' &&
                   js[i] != '\t' && js[i] != '\n' && js[i] != '\r')
                i++;
            if (i == s)
                return -2;
            int t = alloc_tok(toks, cap, &n);
            if (t < 0)
                return -1;
            if (js[s] == 't' || js[s] == 'f')
                toks[t].type = J_BOOL;
            else if (js[s] == 'n')
                toks[t].type = J_NULL;
            else
                toks[t].type = J_NUM;
            toks[t].start = s;
            toks[t].end = i;
            toks[t].parent = parent;
            if (parent >= 0)
                toks[parent].size++;
            continue;
        }
    }
    if (parent >= 0)
        return -2;
    return (int)n;
}

static int tok_eq(const char *js, const jtok_t *t, const char *s) {
    int len = t->end - t->start;
    return (int)strlen(s) == len && memcmp(js + t->start, s, (size_t)len) == 0;
}

int json_obj_get(const char *js, const jtok_t *t, int ntok, int obj, const char *key) {
    if (obj < 0 || obj >= ntok || t[obj].type != J_OBJ)
        return -1;
    int idx = obj + 1;
    for (int k = 0; k < t[obj].size; k++) {
        if (idx + 1 >= ntok)
            return -1;
        if (t[idx].type == J_STR && tok_eq(js, &t[idx], key))
            return idx + 1;

        int depth = 0, j = idx + 1;

        j++;
        while (j < ntok) {
            int p = t[j].parent;
            int inside = 0;
            while (p >= 0) {
                if (p == idx + 1) {
                    inside = 1;
                    break;
                }
                p = t[p].parent;
            }
            if (!inside)
                break;
            j++;
        }
        (void)depth;
        idx = j;
    }
    return -1;
}

static int subtree_end(const jtok_t *t, int ntok, int root) {
    int j = root + 1;
    while (j < ntok) {
        int p = t[j].parent, inside = 0;
        while (p >= 0) {
            if (p == root) {
                inside = 1;
                break;
            }
            p = t[p].parent;
        }
        if (!inside)
            break;
        j++;
    }
    return j;
}

int json_arr_len(const jtok_t *t, int arr) {
    return t[arr].type == J_ARR ? t[arr].size : 0;
}

int json_arr_at(const jtok_t *t, int ntok, int arr, int i) {
    if (t[arr].type != J_ARR || i < 0 || i >= t[arr].size)
        return -1;
    int idx = arr + 1;
    for (int k = 0; k < i; k++)
        idx = subtree_end(t, ntok, idx);
    return idx;
}

static int hex4(const char *text, uint32_t *value) {
    uint32_t result = 0;
    for (int i = 0; i < 4; ++i) {
        unsigned char c = (unsigned char)text[i];
        unsigned digit = c >= '0' && c <= '9'   ? c - '0'
                         : c >= 'a' && c <= 'f' ? c - 'a' + 10
                         : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                : 16;
        if (digit == 16)
            return 0;
        result = result * 16 + digit;
    }
    *value = result;
    return 1;
}

static int utf8(char *out, unsigned cap, unsigned *offset, uint32_t value) {
    unsigned bytes = value < 0x80 ? 1 : value < 0x800 ? 2 : value < 0x10000 ? 3 : 4;
    if (*offset + bytes >= cap)
        return 0;
    if (bytes == 1)
        out[(*offset)++] = (char)value;
    else {
        for (unsigned i = bytes - 1; i; --i) {
            out[*offset + i] = (char)(0x80 | (value & 0x3f));
            value >>= 6;
        }
        out[*offset] = (char)((0xf0 << (4 - bytes)) | value);
        *offset += bytes;
    }
    return 1;
}

int json_str(const char *js, const jtok_t *t, int idx, char *out, unsigned cap) {
    if (idx < 0 || t[idx].type != J_STR || cap == 0)
        return -1;
    unsigned o = 0;
    for (int i = t[idx].start; i < t[idx].end && o + 1 < cap; i++) {
        if (js[i] == '\\' && i + 1 < t[idx].end) {
            char e = js[++i];
            char c = e;
            if (e == 'n')
                c = '\n';
            else if (e == 't')
                c = '\t';
            else if (e == 'r')
                c = '\r';
            else if (e == 'u') {
                uint32_t value;
                if (i + 4 >= t[idx].end || !hex4(js + i + 1, &value))
                    value = 0xfffd;
                else {
                    i += 4;
                    if (value >= 0xd800 && value <= 0xdbff && i + 6 < t[idx].end && js[i + 1] == '\\' &&
                        js[i + 2] == 'u') {
                        uint32_t low;
                        if (hex4(js + i + 3, &low) && low >= 0xdc00 && low <= 0xdfff) {
                            value = 0x10000 + ((value - 0xd800) << 10) + low - 0xdc00;
                            i += 6;
                        } else
                            value = 0xfffd;
                    } else if (value >= 0xd800 && value <= 0xdfff)
                        value = 0xfffd;
                }
                if (!value)
                    value = 0xfffd;
                if (!utf8(out, cap, &o, value))
                    break;
                continue;
            }
            out[o++] = c;
        } else
            out[o++] = js[i];
    }
    out[o] = 0;
    return (int)o;
}

long long json_int(const char *js, const jtok_t *t, int idx) {
    if (idx < 0)
        return 0;
    if (t[idx].type == J_BOOL)
        return (js[t[idx].start] == 't') ? 1 : 0;
    char buf[64];
    int n = t[idx].end - t[idx].start;
    if (n <= 0 || n >= (int)sizeof buf)
        return 0;
    memcpy(buf, js + t[idx].start, (size_t)n);
    buf[n] = 0;
    return strtoll(buf, NULL, 10);
}

void json_write_string(FILE *file, const char *text) {
    fputc('"', file);
    for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', file);
            fputc(*p, file);
        } else if (*p < 32)
            fprintf(file, "\\u%04x", *p);
        else
            fputc(*p, file);
    }
    fputc('"', file);
}
