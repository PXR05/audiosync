# CLI reference

## Help and syntax

~~~text
audiosync --help
audiosync --version
audiosync sync -h
audiosync help profile add
~~~

Global options can appear before or after a command. Required option values are kept intact, even when they look like another flag. Profile options can appear before or after the profile name.

| Option | Meaning |
| --- | --- |
| `-h, --help` | Command-specific help |
| `-V, --version` | Version only |
| `-q, --quiet` | Suppress progress and informational messages; errors remain |
| `-d, --detach` | Run `sync` or `watch` in the background |
| `--json` | Structured output for devices, profile list/show, and status |

Commands, flags, and aliases use lowercase, except `-V`. `--` ends option parsing; subsequent values are positional names.

~~~text
audiosync profile add --serial ID --local ./Music Player
audiosync profile set Player --format=mp3
audiosync profile rename -- Player -Walkman
audiosync profile show -- -Walkman
~~~

## Profiles

`profile` (or `profiles`) groups `list`, `show`, `add`, `set`, `rename`, and `remove`. `ls` aliases `list`, and `rm` aliases `remove`. Every action also works directly: `audiosync ls`, `audiosync add Player ...`.

Profile names must be unique, ignoring case. A serial from `devices` identifies the destination even if its drive letter or mount location changes. Profiles can be added offline using a previously recorded serial.

| Option | Meaning/default |
| --- | --- |
| `--serial ID` | Required for new profiles |
| `--local PATH` | Local source; saved as an absolute path |
| `--url URL` | AudioStream source using HTTP or HTTPS |
| `--adapter NAME` | Remote source adapter; default `audiostream` |
| `--username USER` | AudioStream username |
| `--password` | Hidden interactive password prompt; requires a terminal |
| `--password-stdin` | Read one password line from stdin for scripts |
| `--target DIR` | Safe relative destination folder; default `Music` |
| `--format FORMAT` | `keep`, `mp3`, `opus`, `flac`, `ogg`, `m4a`, `wav`; default `keep` |
| `--layout A\|B\|C\|D` | Remote folder layout; default `D` |
| `--mirror` | Delete destination files absent from the source |
| `--no-mirror` | Keep extra files; default |

Choose one source option. `set` preserves settings you omit, including credentials. Removing a profile keeps its destination files.

~~~powershell
audiosync profile add Player --serial 1234-ABCD --local "C:\Users\you\Music"
audiosync profile set Player --target Audio/Albums --format mp3
audiosync profile add Remote --serial 5678-EFGH --url https://your-server --username you --password
~~~

Windows protects passwords with DPAPI; Linux uses Secret Service. An unavailable keyring does not erase stored credentials during unrelated edits. No plaintext or encrypted password is included in `list`/`show` output. Pipe password-manager output to `--password-stdin` for noninteractive use; avoid putting passwords in command-line arguments.

## Sync and watch

~~~text
audiosync sync
audiosync sync Player
audiosync sync -p Player
audiosync sync --profile=Player --quiet
audiosync sync -d
audiosync watch
audiosync watch -d
audiosync watch --no-tray
~~~

A sync selector matches profile name, serial, or label. Omit it to sync all connected profiles. Another sync cannot start while one is active; use `status -w` to observe it instead.

Terminal progress goes to stderr. When stderr is redirected, progress uses ordinary lines without carriage-return animations. `--quiet` suppresses progress and informational messages while retaining failures and the exit code.

Use `-d` to start `sync` or `watch` in the background. During an attached terminal run, press `d`
to detach. Follow it later with `audiosync status -w` or `audiosync logs -f`.

The tray menu can sync immediately, pause automatic triggers, manage startup, and exit. Windows shortcuts/login startup run `watch` without retaining a private console window. On Linux, autostart writes `~/.config/autostart/audiosync.desktop` for the current desktop user. Watch starts again after login when autostart is enabled; pause state is not retained across restarts. Headless watch stays in the foreground unless detached. Ctrl+C or SIGTERM stops the watcher after the active transfer finishes.

## Inspect live progress

~~~text
audiosync status
audiosync status -w
audiosync status --json
audiosync status -w --json
~~~

Status shows whether the watcher and sync are running, the current file, file index/count, per-file byte percentage when available, and accumulated new/skipped/failed counts. Snapshots update at most four times per second during byte progress. The last result remains available after completion. If the process ended without reporting completion, status marks the last run interrupted.

`-w/--watch` follows changes; Ctrl+C stops observing without stopping the sync. With `--json`, a one-shot request produces one JSON object and following produces newline-delimited JSON objects. Results and progress describe the entire sync invocation; file index/count describe the currently processed device or stage.

An older running executable has no progress snapshots. Restart it with the current build to enable live progress.

## Activity logs

~~~text
audiosync logs
audiosync logs -n 100
audiosync logs -fn50
audiosync logs --follow --lines=0
audiosync logs --path
~~~

The default is the last 30 lines. `-n/--lines` accepts 0 through 10000. `-f/--follow` reads appended entries and continues across log rotation. Ctrl+C stops following without affecting sync.

The activity log is now `audiosync.log` beside the configuration file on both operating systems. It rotates at approximately 512 KiB, retaining one `audiosync.log.old`. Previous Windows versions wrote to `%TEMP%\audiosync.log`; that historical file is not migrated.

## Updates

`audiosync update --check` reports whether a newer release is available. `audiosync update` replaces an
installed AppImage on Linux or starts the update installer on Windows. Package-manager and portable archive
installs should be updated through the same method used to install them.

## Configuration and scripting

`audiosync config path` (also `config-path`) prints the configuration path:

- Windows: `%APPDATA%\audiosync\config.json`
- Linux: `$XDG_CONFIG_HOME/audiosync/config.json`, defaulting to `~/.config/audiosync/config.json`

Profiles use the existing configuration format. Writes replace the file atomically; keep encrypted credentials on their original OS/user account. Runtime status and logs are stored beside the config; observing status never starts a sync.

Human-readable listings show names, connection status, and labeled fields. Use JSON for automation rather than parsing this display:

~~~powershell
$profiles = audiosync profile ls --json | ConvertFrom-Json
$state = audiosync status --json | ConvertFrom-Json
~~~

Stdout carries requested results. Diagnostics and progress go to stderr. An empty JSON list is `[]`; a missing last-sync snapshot is `null`.

| Exit code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Operation failed or runtime/configuration lock unavailable |
| 2 | Invalid command, option, or profile arguments |
| 3 | Sync had no matching mounted profile |
| 130 | Status/log following interrupted |

## Development

See the [source layout and editor setup](../README.md#source-layout) in the README.
