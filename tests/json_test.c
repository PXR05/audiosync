#include "json.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void) {
    const char *json = "{\"text\":\"caf\\u00e9 \\u65e5\\u672c\\u8a9e \\ud83c\\udfb5\"}";
    jtok_t tokens[3];
    char text[32];
    int count = json_parse(json, (unsigned)strlen(json), tokens, 3);
    int value = json_obj_get(json, tokens, count, 0, "text");
    assert(json_str(json, tokens, value, text, sizeof text) > 0);
    assert(!strcmp(text, "caf\xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x8e\xb5"));
    puts("PASS: JSON Unicode escapes and surrogate pairs");
    return 0;
}
