#include "cli/internal.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#ifndef AUDIOSYNC_VERSION
#define AUDIOSYNC_VERSION "dev"
#endif
typedef struct {
    const char *name, *summary, *usage, *options, *example;
} help_t;
static const char *profile_options =
    "  --serial ID           Mounted drive serial (see 'devices')\n"
    "  --local PATH          Local source folder; saved as an absolute path\n"
    "  --url URL             AudioStream server URL (http/https)\n"
    "  --adapter NAME        Remote source adapter [audiostream]\n"
    "  --username USER       AudioStream username\n"
    "  --password            Prompt for a password with echo disabled\n"
    "  --password-stdin      Read a password from stdin (for scripts)\n"
    "  --target DIR          Relative destination folder [Music]\n"
    "  --format FORMAT       keep, mp3, opus, flac, ogg, m4a, wav [keep]\n"
    "  --layout A|B|C|D      Remote folder layout [D]\n"
    "  --mirror              Delete destination files absent from the source\n"
    "  --no-mirror           Keep extra destination files [default]\n";
static const help_t commands[] = {
    {"devices", "List mounted drives.", "devices [--json]", "", "audiosync devices"},
    {"list", "List saved profiles and connection status.", "profile list [--json]", "",
     "audiosync profile ls"},
    {"show", "Show one profile without exposing credentials.", "profile show NAME [--json]", "",
     "audiosync profile show Player"},
    {"add", "Create a profile. The destination can be unplugged.",
     "profile add NAME --serial ID (--local PATH | --url URL) [options]", "",
     "audiosync profile add Player --serial 1234-ABCD --local ./Music"},
    {"set", "Update only the supplied profile settings.", "profile set NAME [options]", "",
     "audiosync profile set Player --format=mp3"},
    {"remove", "Remove a saved profile. Destination files are kept.", "profile remove NAME", "",
     "audiosync profile rm Player"},
    {"rename", "Rename a saved profile.", "profile rename OLD NEW", "",
     "audiosync profile rename Player Walkman"},
    {"sync", "Sync once. Omit the selector to sync all connected profiles.",
     "sync [NAME|SERIAL|LABEL] [-d] [-q]",
     "  -d, --detach          Run in the background\n"
     "  -q, --quiet           Suppress progress and informational messages\n"
     "  -p, --profile NAME    Select a profile (alternative to a positional selector)\n",
     "audiosync sync Player"},
    {"watch", "Run automatic syncing and the tray menu until stopped.", "watch [--no-tray] [-d] [-q]",
     "  --no-tray             Stay in the terminal without a tray icon\n"
     "  -d, --detach          Run in the background\n"
     "  -q, --quiet           Suppress informational messages\n",
     "audiosync watch --no-tray"},
    {"status", "Inspect the running watcher, sync progress, and last result.", "status [-w] [--json]",
     "  -w, --watch           Follow status changes; Ctrl+C stops watching\n"
     "  --json                JSON snapshot, or JSON Lines when following\n",
     "audiosync status --watch"},
    {"logs", "Read the activity log for this configuration.", "logs [-f] [-n LINES] [--path]",
     "  -f, --follow          Follow new log entries; Ctrl+C stops following\n"
     "  -n, --lines N         Number of recent lines [30], from 0 to 10000\n"
     "  --path                Print the log file path\n",
     "audiosync logs -f -n 50"},
    {"config-path", "Print the configuration path.", "config path", "", "audiosync config path"},
    {"autostart", "Enable or disable tray startup at login.", "autostart on|off|status", "",
     "audiosync autostart on"},
    {"update", "Install the latest AudioSync release.", "update [--check]",
     "  --check               Check without installing\n", "audiosync update"}};
int cli_error(const char *command, const char *format, ...) {
    fputs("error: ", stderr);
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputc('\n', stderr);
    fprintf(stderr, "Try 'audiosync %s%s--help' for usage.\n", command ? command : "", command ? " " : "");
    return 2;
}
int cli_help(const char *command) {
    if (!command || !strcmp(command, "help")) {
        printf("AudioSync %s - sync your audio to removable storage\n\n", AUDIOSYNC_VERSION);
        puts("Usage: audiosync [options] <command> [args]\n\n"
             "Commands:\n"
             "  devices          List mounted drives and serial IDs\n"
             "  profile          Manage profiles: list, show, add, set, rename, remove\n"
             "  sync             Sync once with terminal progress\n"
             "  watch            Start the automatic sync watcher and tray\n"
             "  status           Inspect or follow a running sync\n"
             "  logs             Read or follow the activity log\n"
             "  config path      Print the configuration path\n"
             "  autostart        Configure startup at login\n"
             "  update           Check for and install updates\n\n"
             "Options:\n"
             "  -h, --help       Show help (also: help COMMAND)\n"
             "  -V, --version    Print version\n"
             "  -d, --detach     Run sync or watch in the background\n"
             "  -q, --quiet      Suppress informational output\n"
             "  --json           Structured output for devices, profiles, and status\n\n"
             "Examples:\n"
             "  audiosync devices\n"
             "  audiosync profile add Player --serial ID --local ./Music\n"
             "  audiosync sync Player\n"
             "  audiosync status -w\n"
             "  audiosync logs -f\n\n"
             "Use 'audiosync COMMAND --help' for options and examples.\n"
             "Aliases: ls/list, rm/remove; profile actions also work at the top level.\n"
             "Long options accept --option=value. Use -- to end option parsing.");
        return 0;
    }
    if (!strcmp(command, "profile")) {
        puts("Manage saved device profiles.\n\n"
             "Usage: audiosync profile <list|show|add|set|rename|remove> [args]\n\n"
             "Aliases: ls for list, rm for remove; 'profiles' also works.\n"
             "Use 'audiosync profile ACTION --help' for details.");
        return 0;
    }
    if (!strcmp(command, "config")) {
        puts("Usage: audiosync config path\n\nPrint the configuration file path.");
        return 0;
    }
    for (unsigned i = 0; i < sizeof commands / sizeof *commands; ++i) {
        const help_t *help = &commands[i];
        if (strcmp(help->name, command))
            continue;
        printf("%s\n\nUsage: audiosync %s\n\n", help->summary, help->usage);
        if (!strcmp(command, "add") || !strcmp(command, "set"))
            fputs(profile_options, stdout);
        else
            fputs(help->options, stdout);
        printf("\n  -h, --help           Show this help\n\nExample:\n  %s\n", help->example);
        if (!strcmp(command, "sync"))
            puts("\nExit codes: 0 success, 1 failure/busy, 2 invalid arguments, 3 no matching mounted "
                 "device.\n"
                 "Progress goes to stderr. Existing destination files are skipped.\n"
                 "To inspect an active tray sync, use 'audiosync status -w'.");
        return 0;
    }
    return cli_error(NULL, "unknown command '%s'", command);
}
