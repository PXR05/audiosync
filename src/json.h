#ifndef JSON_MIN_H
#define JSON_MIN_H
#include <stdio.h>

typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } jtype_t;

typedef struct {
    jtype_t type;
    int start, end;
    int size;
    int parent;
} jtok_t;

int json_parse(const char *js, unsigned len, jtok_t *toks, unsigned cap);

int json_obj_get(const char *js, const jtok_t *t, int ntok, int obj, const char *key);

int json_arr_len(const jtok_t *t, int arr);

int json_arr_at(const jtok_t *t, int ntok, int arr, int i);

int json_str(const char *js, const jtok_t *t, int idx, char *out, unsigned cap);

long long json_int(const char *js, const jtok_t *t, int idx);

void json_write_string(FILE *file, const char *text);
#endif
