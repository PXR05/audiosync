#ifndef CONFIG_H
#define CONFIG_H

#define CFG_LAYOUT_A 0
#define CFG_LAYOUT_B 1
#define CFG_LAYOUT_C 2
#define CFG_LAYOUT_D 3

typedef struct {
    char name[64];
    char serial[128];
    char label[64];
    char target_subdir[128];
    char source_type[32];
    char base_url[256];
    char username[64];
    char password[256];
    char password_enc[1024];
    char local_path[1024];
    int layout;
    int mirror;
    char format[16];

} device_cfg_t;

typedef struct {
    device_cfg_t *devs;
    int n;
} config_t;

char *config_path(void);
int config_load(config_t *c);
int config_save(const config_t *c);
void config_free(config_t *c);
char layout_to_char(int l);
int char_to_layout(char c);

#endif
