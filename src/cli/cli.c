#include "cli/cli.h"
#include "cli/internal.h"
#include "progress.h"
#include "log.h"
#include "platform/util.h"
#include "platform/runtime.h"
#include "platform/tray.h"
#include <stdlib.h>
#include <string.h>
#ifndef AUDIOSYNC_VERSION
#define AUDIOSYNC_VERSION "dev"
#endif
static int value_option(const char *option) {
    return cli_profile_option(option) == 1 || !strcmp(option, "--lines") || !strcmp(option, "--profile");
}
static const char *short_option(char c) {
    switch (c) {
    case 'h':
        return "--help";
    case 'V':
        return "--version";
    case 'q':
        return "--quiet";
    case 'f':
        return "--follow";
    case 'n':
        return "--lines";
    case 'w':
        return "--watch";
    case 'p':
        return "--profile";
    default:
        return NULL;
    }
}
static const char *canonical(const char *name) {
    if (!strcmp(name, "ls"))
        return "list";
    if (!strcmp(name, "rm"))
        return "remove";
    if (!strcmp(name, "profiles"))
        return "profile";
    return name;
}
static int dispatch(int argc, char **argv, int json, int quiet, int help, int literal) {
    if (argc == 1)
        return cli_help(NULL);
    const char *command = canonical(argv[1]);
    argv[1] = (char *)command;
    if (!strcmp(command, "help")) {
        if (argc == 2)
            return cli_help(NULL);
        if (argc == 3)
            return cli_help(canonical(argv[2]));
        if (argc == 4 && (!strcmp(argv[2], "profile") || !strcmp(argv[2], "profiles")))
            return cli_help(canonical(argv[3]));
        return cli_error(NULL, "expected 'help [COMMAND]' or 'help profile ACTION'");
    }
    if (!strcmp(command, "profile")) {
        if (argc == 2)
            return cli_help("profile");
        const char *action = canonical(argv[2]);
        const char *actions[] = {"list", "show", "add", "set", "remove", "rename"};
        int valid = 0;
        for (unsigned i = 0; i < sizeof actions / sizeof *actions; ++i)
            if (!strcmp(action, actions[i]))
                valid = 1;
        if (!valid)
            return cli_error("profile", "unknown action '%s'", argv[2]);
        argv[2] = (char *)action;
        argv++;
        argc--;
        command = action;
    } else if (!strcmp(command, "config")) {
        if (argc == 2)
            return cli_help("config");
        if (argc != 3 || strcmp(argv[2], "path"))
            return cli_error("config", "expected 'config path'");
        command = "config-path";
        argv[1] = (char *)command;
        argc = 2;
    }
    if (help)
        return cli_help(command);
    int read_profile = !strcmp(command, "list") || !strcmp(command, "show");
    if (json && !(read_profile || !strcmp(command, "devices") || !strcmp(command, "status")))
        return cli_error(command, "--json is only available for devices, profile list/show, and status");
    if (!strcmp(command, "devices"))
        return argc == 2 ? cli_devices(json) : cli_error(command, "unexpected argument '%s'", argv[2]);
    if (!strcmp(command, "config-path")) {
        if (argc != 2)
            return cli_error(command, "unexpected arguments");
        char *path = config_path();
        if (!path) {
            fputs("error: cannot locate configuration directory\n", stderr);
            return 1;
        }
        puts(path);
        free(path);
        return 0;
    }
    if (!strcmp(command, "status") || !strcmp(command, "logs"))
        return cli_observe(argc, argv, json);
    if (!strcmp(command, "sync")) {
        const char *only = NULL;
        if (argc == 4 && (!strcmp(argv[2], "--profile") || !strcmp(argv[2], "--")))
            only = argv[3];
        else if (argc == 3 && (literal || argv[2][0] != '-'))
            only = argv[2];
        else if (argc != 2)
            return cli_error(command, "expected one selector or --profile NAME");
        if (only && !*only)
            return cli_error(command, "profile selector cannot be empty");
        progress_set(quiet ? NULL : &progress_cli);
        return runtime_sync(only);
    }
    if (!strcmp(command, "watch")) {
        if (argc != 2 && !(argc == 3 && !strcmp(argv[2], "--no-tray")))
            return cli_error(command, "expected watch [--no-tray]");
        if (!runtime_lock(LOCK_WATCH)) {
            fputs("error: watcher already running or unavailable; use 'audiosync status -w' to inspect it\n",
                  stderr);
            return 1;
        }
        if (argc == 3 && !quiet)
            progress_set(&progress_cli);
        int result = tray_run(argc == 3);
        runtime_unlock(LOCK_WATCH);
        return result;
    }
    if (!strcmp(command, "autostart")) {
        if (argc != 3)
            return cli_error(command, "choose on, off, or status");
        int result;
        if (!strcmp(argv[2], "status")) {
            puts(autostart_is_enabled() ? "on" : "off");
            return 0;
        }
        if (!strcmp(argv[2], "on"))
            result = autostart_enable();
        else if (!strcmp(argv[2], "off"))
            result = autostart_disable();
        else
            return cli_error(command, "unknown setting '%s'; choose on, off, or status", argv[2]);
        if (result)
            fputs("error: could not update startup at login\n", stderr);
        else if (!quiet)
            printf("Startup at login: %s.\n", argv[2]);
        return result;
    }
    if (read_profile || !strcmp(command, "add") || !strcmp(command, "set") || !strcmp(command, "remove") ||
        !strcmp(command, "rename"))
        return cli_profiles(argc, argv, json, quiet);
    return cli_error(NULL, "unknown command '%s'", command);
}
int cli_run(int argc, char **argv) {
    if (argc <= 0 || !argv)
        return 2;
    /* Normalize option spellings without treating option values as commands or flags. */
    size_t capacity = 1;
    for (int i = 0; i < argc; ++i)
        capacity += strlen(argv[i]) + 2;
    char **args = calloc(capacity, sizeof *args);
    char **owned = calloc((size_t)argc, sizeof *owned);
    if (!args || !owned) {
        free(args);
        free(owned);
        return 1;
    }
    int count = 1, allocations = 0, literal = 0, waiting = 0, result = 0;
    args[0] = argv[0];
    for (int i = 1; i < argc; ++i) {
        char *arg = argv[i];
        if (waiting || literal) {
            args[count++] = arg;
            waiting = 0;
            continue;
        }
        if (!strcmp(arg, "--")) {
            literal = 1;
            args[count++] = arg;
            continue;
        }
        if (!strncmp(arg, "--", 2)) {
            char *equal = strchr(arg, '=');
            if (equal) {
                char *copy = str_dup(arg);
                if (!copy) {
                    result = 1;
                    goto cleanup;
                }
                owned[allocations++] = copy;
                copy[equal - arg] = 0;
                if (!value_option(copy)) {
                    result = cli_error(NULL, "'%s' does not accept a value", copy);
                    goto cleanup;
                }
                args[count++] = copy;
                args[count++] = copy + (equal - arg) + 1;
            } else {
                args[count++] = arg;
                waiting = value_option(arg);
            }
        } else if (arg[0] == '-' && arg[1]) {
            for (int j = 1; arg[j]; ++j) {
                const char *option = short_option(arg[j]);
                if (!option) {
                    result = cli_error(NULL, "unknown option '-%c'", arg[j]);
                    goto cleanup;
                }
                args[count++] = (char *)option;
                if (value_option(option)) {
                    if (arg[j + 1])
                        args[count++] = arg + j + 1;
                    else
                        waiting = 1;
                    break;
                }
            }
        } else
            args[count++] = arg;
    }
    if (waiting) {
        result = cli_error(NULL, "option '%s' needs a value", args[count - 1]);
        goto cleanup;
    }
    int json = 0, quiet = 0, help = 0, version = 0, output = 1;
    literal = 0;
    for (int i = 1; i < count; ++i) {
        char *arg = args[i];
        if (!literal && !strcmp(arg, "--")) {
            literal = 1;
            if (output > 1 && i + 1 < count)
                args[output++] = arg;
            continue;
        }
        if (!literal && !strcmp(arg, "--help"))
            help = 1;
        else if (!literal && !strcmp(arg, "--version"))
            version = 1;
        else if (!literal && !strcmp(arg, "--quiet"))
            quiet = 1;
        else if (!literal && !strcmp(arg, "--json"))
            json = 1;
        else {
            args[output++] = arg;
            if (!literal && value_option(arg) && i + 1 < count)
                args[output++] = args[++i];
        }
    }
    log_set_quiet(quiet);
    if (version) {
        if (output != 1)
            result = cli_error(NULL, "--version cannot be combined with a command");
        else
            printf("audiosync %s\n", AUDIOSYNC_VERSION);
    } else
        result = dispatch(output, args, json, quiet, help, literal);
cleanup:
    for (int i = 0; i < allocations; ++i)
        free(owned[i]);
    free(owned);
    free(args);
    return result;
}
