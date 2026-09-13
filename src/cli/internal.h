#ifndef AUDIOSYNC_CLI_INTERNAL_H
#define AUDIOSYNC_CLI_INTERNAL_H
#include "config.h"
#include <stdio.h>
int cli_help(const char *command);
int cli_error(const char *command, const char *format, ...);
int cli_profiles(int argc, char **argv, int json, int quiet);
/* -1 unknown, 0 flag, 1 value. */
int cli_profile_option(const char *option);
int cli_devices(int json);
int cli_observe(int argc, char **argv, int json);
int cli_update(int argc, char **argv, int quiet);
int cli_is_terminal(FILE *stream);
void cli_print_profile(const device_cfg_t *device, int json, int connected);
#endif
