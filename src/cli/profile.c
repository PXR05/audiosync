#include "cli/internal.h"
#include "adapters/adapter.h"
#include "platform/util.h"
#include "platform/devices.h"
#include "platform/runtime.h"
#include "sync.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <conio.h>
#else
#include <termios.h>
#include <unistd.h>
#include <signal.h>
static volatile sig_atomic_t password_interrupted;
static void password_signal(int value) {
    (void)value;
    password_interrupted = 1;
}
#endif
int cli_profile_option(const char *option) {
    static const struct {
        const char *name;
        int value;
    } options[] = {
        {"--serial", 1},   {"--local", 1},     {"--url", 1},       {"--adapter", 1},
        {"--username", 1}, {"--target", 1},    {"--layout", 1},    {"--format", 1},
        {"--mirror", 0},   {"--no-mirror", 0}, {"--password", 0},  {"--password-stdin", 0},
    };
    for (size_t i = 0; i < sizeof options / sizeof *options; ++i)
        if (!strcmp(option, options[i].name))
            return options[i].value;
    return -1;
}
static int read_password(char *out, size_t cap) {
    if (!cli_is_terminal(stdin)) {
        fputs("error: --password needs a terminal; use --password-stdin for piped input\n", stderr);
        return 0;
    }
    fputs("Password: ", stderr);
    fflush(stderr);
#ifdef _WIN32
    wchar_t wide[256] = {0};
    size_t n = 0;
    int ok = 1;
    for (;;) {
        wint_t ch = _getwch();
        if (ch == 3 || ch == WEOF) {
            ok = 0;
            break;
        }
        if (ch == '\r' || ch == '\n')
            break;
        if (ch == 0 || ch == 0xe0) {
            _getwch();
            continue;
        }
        if (ch == '\b') {
            if (n)
                wide[--n] = 0;
            continue;
        }
        if (n + 1 >= sizeof wide / sizeof *wide) {
            ok = 0;
            break;
        }
        wide[n++] = (wchar_t)ch;
    }
    char *text = ok ? wide_to_utf8(wide) : NULL;
    ok = text && strlen(text) < cap;
    if (ok)
        strcpy(out, text);
    if (text) {
        memset(text, 0, strlen(text));
        free(text);
    }
    memset(wide, 0, sizeof wide);
#else
    struct termios saved, hidden;
    if (tcgetattr(STDIN_FILENO, &saved))
        return 0;
    hidden = saved;
    hidden.c_lflag &= (tcflag_t)~ECHO;
    struct sigaction action = {0}, old_int, old_term;
    action.sa_handler = password_signal;
    sigemptyset(&action.sa_mask);
    password_interrupted = 0;
    sigaction(SIGINT, &action, &old_int);
    sigaction(SIGTERM, &action, &old_term);
    int ok = tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden) == 0;
    char buffer[258] = {0};
    if (ok)
        ok = fgets(buffer, sizeof buffer, stdin) != NULL && !password_interrupted;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    buffer[strcspn(buffer, "\r\n")] = 0;
    ok = ok && strlen(buffer) < cap;
    if (ok)
        strcpy(out, buffer);
    memset(buffer, 0, sizeof buffer);
#endif
    fputc('\n', stderr);
    if (!ok)
        fputs("error: password input failed, was interrupted, or exceeded 255 bytes\n", stderr);
    return ok;
}
static int copy_value(char *out, size_t cap, const char *value) {
    if (strlen(value) >= cap) {
        fprintf(stderr, "error: option value exceeds %zu bytes\n", cap - 1);
        return 0;
    }
    strcpy(out, value);
    return 1;
}
static int profile_index(const config_t *cfg, const char *name) {
    int found = -1;
    for (int i = 0; i < cfg->n; ++i)
        if (text_equal_ci(cfg->devs[i].name, name)) {
            if (found >= 0) {
                fputs("Ambiguous profile name; make names unique in the configuration\n", stderr);
                return -2;
            }
            found = i;
        }
    return found;
}
static int edit_profile(int argc, char **argv, config_t *cfg, int add, int quiet) {
    const char *name = argv[2];
    if (!*name)
        return cli_error(argv[1], "invalid or missing profile option");
    int index = profile_index(cfg, name);
    if ((add && index != -1) || (!add && index < 0)) {
        fputs(add ? "Profile already exists\n" : "Profile not found or ambiguous\n", stderr);
        return 2;
    }
    device_cfg_t device = {0};
    if (!add)
        device = cfg->devs[index];
    else {
        strcpy(device.target_subdir, "Music");
        strcpy(device.format, "keep");
        device.layout = CFG_LAYOUT_D;
    }
    if (!copy_value(device.name, sizeof device.name, name))
        return 2;
    int password_stdin = 0, password_prompt = 0, source = 0, adapter_set = 0;
    for (int i = 3; i < argc; ++i) {
        const char *key = argv[i];
        if (!strcmp(key, "--mirror")) {
            device.mirror = 1;
            continue;
        }
        if (!strcmp(key, "--no-mirror")) {
            device.mirror = 0;
            continue;
        }
        if (!strcmp(key, "--password")) {
            password_prompt = 1;
            continue;
        }
        if (!strcmp(key, "--password-stdin")) {
            password_stdin = 1;
            continue;
        }
        if (i + 1 >= argc)
            return cli_error(argv[1], "invalid or missing profile option");
        const char *value = argv[++i];
        char *field = NULL;
        size_t cap = 0;
        if (!strcmp(key, "--serial")) {
            field = device.serial;
            cap = sizeof device.serial;
        } else if (!strcmp(key, "--target")) {
            field = device.target_subdir;
            cap = sizeof device.target_subdir;
        } else if (!strcmp(key, "--username")) {
            field = device.username;
            cap = sizeof device.username;
        } else if (!strcmp(key, "--format")) {
            field = device.format;
            cap = sizeof device.format;
        } else if (!strcmp(key, "--local")) {
            if (source++)
                return cli_error(argv[1], "choose only one source: --local or --url");
            strcpy(device.source_type, "local");
            field = device.local_path;
            cap = sizeof device.local_path;
        } else if (!strcmp(key, "--url")) {
            if (source++)
                return cli_error(argv[1], "choose only one source: --local or --url");
            if (!adapter_set)
                strcpy(device.source_type, "audiostream");
            field = device.base_url;
            cap = sizeof device.base_url;
        } else if (!strcmp(key, "--adapter")) {
            adapter_set = 1;
            field = device.source_type;
            cap = sizeof device.source_type;
        } else if (!strcmp(key, "--layout")) {
            if (strlen(value) != 1 || !strchr("ABCDabcd", *value))
                return cli_error(argv[1], "--layout must be A, B, C, or D");
            device.layout = char_to_layout(*value);
        } else {
            fprintf(stderr, "Unknown option: %s\n", key);
            return 2;
        }
        if (field && !copy_value(field, cap, value))
            return 2;
    }
    if (!device.serial[0])
        return cli_error(argv[1], "--serial is required; find it with 'audiosync devices'");
    if (!sync_target_valid(device.target_subdir))
        return cli_error(argv[1], "--target must be a relative folder without '..' or reserved characters");
    const char *formats[] = {"keep", "mp3", "opus", "flac", "ogg", "m4a", "wav"};
    int valid = 0;
    for (unsigned i = 0; i < sizeof formats / sizeof *formats; ++i)
        if (!strcmp(device.format, formats[i]))
            valid = 1;
    if (!valid)
        return cli_error(argv[1], "unknown format '%s' (use keep, mp3, opus, flac, ogg, m4a, or wav)",
                         device.format);
    if (!strcmp(device.source_type, "local")) {
        if (!device.local_path[0])
            return cli_error(argv[1], "invalid or missing profile option");
        wchar_t *path = utf8_to_wide(device.local_path);
        wchar_t absolute[MAX_PATH * 2];
        DWORD length = path ? GetFullPathNameW(path, MAX_PATH * 2, absolute, NULL) : 0;
        free(path);
        char *normalized = length && length < MAX_PATH * 2 ? wide_to_utf8(absolute) : NULL;
        if (!normalized || !copy_value(device.local_path, sizeof device.local_path, normalized)) {
            free(normalized);
            return 2;
        }
        free(normalized);
        if (password_stdin || password_prompt)
            return cli_error(argv[1], "password options require a remote source (--url)");
        if (adapter_set)
            return cli_error(argv[1], "--adapter requires a remote source (--url)");
        device.password[0] = 0;
        device.password_enc[0] = 0;
        device.username[0] = 0;
        device.base_url[0] = 0;
    } else {
        const remote_adapter_t *adapter = remote_adapter_find(device.source_type);
        if (!adapter)
            return cli_error(argv[1], "unknown source adapter '%s'", device.source_type);
        if ((strncmp(device.base_url, "https://", 8) && strncmp(device.base_url, "http://", 7)) ||
            (adapter->requires_username && !device.username[0])) {
            fputs("A http/https server URL and the adapter's required credentials are needed\n", stderr);
            return 2;
        }
        device.local_path[0] = 0;
    }
    if (password_stdin && password_prompt)
        return cli_error(argv[1], "use --password or --password-stdin, not both");
    if (password_prompt) {
        device.password_enc[0] = 0;
        if (!read_password(device.password, sizeof device.password))
            return 1;
    }
    if (password_stdin) {
        device.password_enc[0] = 0;
        char password[258];
        if (!fgets(password, sizeof password, stdin)) {
            fputs("Could not read password from stdin\n", stderr);
            return 1;
        }
        password[strcspn(password, "\r\n")] = 0;
        if (!copy_value(device.password, sizeof device.password, password)) {
            memset(password, 0, sizeof password);
            return 2;
        }
        memset(password, 0, sizeof password);
    }
    drive_info_t drives[64];
    int count = devices_list(drives, 64);
    const drive_info_t *drive = devices_find_by_serial(drives, count, device.serial);
    if (drive)
        copy_value(device.label, sizeof device.label, drive->label);
    else if (add || strcmp(cfg->devs[index].serial, device.serial))
        device.label[0] = 0;
    if (add) {
        device_cfg_t *next = realloc(cfg->devs, ((size_t)cfg->n + 1) * sizeof *next);
        if (!next)
            return 1;
        cfg->devs = next;
        index = cfg->n++;
    }
    cfg->devs[index] = device;
    memset(device.password, 0, sizeof device.password);
    if (config_save(cfg) != 0) {
        fputs("Could not save configuration\n", stderr);
        return 1;
    }
    if (!quiet)
        printf("%s profile '%s'.\n", add ? "Added" : "Updated", name);
    return 0;
}

static int profiles_impl(int argc, char **argv, int json, int quiet) {
    const char *command = argv[1];
    int add = !strcmp(command, "add"), set = !strcmp(command, "set");
    int remove = !strcmp(command, "remove"), list = !strcmp(command, "list");
    int show = !strcmp(command, "show"), rename = !strcmp(command, "rename");
    if (!((list && argc == 2) || ((remove || show) && argc == 3) || (rename && argc == 4) ||
          ((add || set) && argc >= 3)))
        return cli_error(command, "unexpected arguments or missing profile name");
    if (!runtime_lock(LOCK_CONFIG)) {
        fputs("error: configuration is busy or unavailable; try again shortly\n", stderr);
        return 1;
    }
    config_t cfg;
    int loaded = config_load(&cfg), result = 0;
    if (loaded != 0 && loaded != -1) {
        fputs("error: cannot read configuration; existing file was not changed\n", stderr);
        result = 1;
    } else if (list || show) {
        int index = show ? profile_index(&cfg, argv[2]) : -1;
        if (show && index < 0)
            result = cli_error(command, "profile '%s' not found or ambiguous; run 'audiosync profile list'",
                               argv[2]);
        else {
            drive_info_t drives[64];
            int count = devices_list(drives, 64);
            if (list && json)
                putchar('[');
            if (list && !json && !cfg.n)
                puts("No profiles yet. Start with 'audiosync profile add --help'.");
            for (int i = 0; i < cfg.n; ++i) {
                if (show && i != index)
                    continue;
                if (list && i && json)
                    putchar(',');
                if (list && i && !json)
                    putchar('\n');
                cli_print_profile(&cfg.devs[i], json,
                                  devices_find_by_serial(drives, count, cfg.devs[i].serial) != NULL);
            }
            if (list && json)
                putchar(']');
            if (json)
                putchar('\n');
        }
    } else if (remove || rename) {
        int index = profile_index(&cfg, argv[2]);
        if (index < 0)
            result = cli_error(command, "profile '%s' not found or ambiguous", argv[2]);
        else if (rename && (!argv[3][0] || strlen(argv[3]) >= sizeof cfg.devs[index].name ||
                            profile_index(&cfg, argv[3]) != -1))
            result = cli_error(command, "new name must be unique and contain 1 to 63 bytes");
        else {
            if (rename)
                strcpy(cfg.devs[index].name, argv[3]);
            else {
                memmove(cfg.devs + index, cfg.devs + index + 1,
                        (size_t)(cfg.n - index - 1) * sizeof *cfg.devs);
                cfg.n--;
            }
            result = config_save(&cfg) ? 1 : 0;
            if (result)
                fputs("error: could not save configuration\n", stderr);
            else if (!quiet)
                printf("%s profile '%s'.\n", rename ? "Renamed" : "Removed", argv[2]);
        }
    } else
        result = edit_profile(argc, argv, &cfg, add, quiet);
    config_free(&cfg);
    runtime_unlock(LOCK_CONFIG);
    return result;
}

int cli_profiles(int argc, char **argv, int json, int quiet) {
    char **ordered = calloc((size_t)argc + 2, sizeof *ordered);
    char **options = calloc((size_t)argc + 2, sizeof *options);
    if (!ordered || !options) {
        free(ordered);
        free(options);
        return 1;
    }
    int count = 2, noptions = 0, literal = 0, result = 0;
    ordered[0] = argv[0];
    ordered[1] = argv[1];
    int edit = !strcmp(argv[1], "add") || !strcmp(argv[1], "set");
    for (int i = 2; i < argc; ++i) {
        const char *arg = argv[i];
        if (!literal && !strcmp(arg, "--")) {
            literal = 1;
            continue;
        }
        if (!literal && arg[0] == '-') {
            if (!edit) {
                result = cli_error(argv[1], "unexpected option '%s'", arg);
                goto done;
            }
            int value = cli_profile_option(arg);
            if (value < 0) {
                result = cli_error(argv[1], "unknown option '%s'", arg);
                goto done;
            }
            options[noptions++] = argv[i];
            if (value) {
                if (i + 1 >= argc) {
                    result = cli_error(argv[1], "'%s' needs a value", arg);
                    goto done;
                }
                options[noptions++] = argv[++i];
            }
        } else
            ordered[count++] = argv[i];
    }
    if ((edit && count != 3) || (!strcmp(argv[1], "rename") && count != 4)) {
        result = cli_error(argv[1], "expected %s", edit ? "one profile name" : "OLD and NEW profile names");
        goto done;
    }
    for (int i = 0; i < noptions; ++i)
        ordered[count++] = options[i];
    result = profiles_impl(count, ordered, json, quiet);
done:
    free(ordered);
    free(options);
    return result;
}
