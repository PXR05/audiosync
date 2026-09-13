# AudioSync

AudioSync copies music from a local folder or remote source to removable storage on Windows and Linux.
It supports saved device profiles, automatic syncing, mirror mode, format conversion, terminal progress,
and a system tray watcher.

## Quick start

### Windows

Download the file ending in `windows-x64-setup.exe` from the
[latest release](https://github.com/PXR05/audiosync/releases/latest), then run it.

### Linux

Install on x86-64 or ARM64:

```bash
curl -fsSL https://raw.githubusercontent.com/PXR05/audiosync/main/install.sh | bash
```

```text
audiosync devices
audiosync profile add Player --serial YOUR_SERIAL --local "/path/to/Music"
audiosync sync Player
audiosync watch
```

Run `audiosync --help` for command help. Format conversion requires FFmpeg on `PATH`.

## Documentation

- [CLI reference](docs/cli.md)
- [Build, test, and project structure](docs/development.md)
- [Creating remote source adapters](docs/adapters.md)
