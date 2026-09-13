# Development

## Build and test

### Windows

Install CMake and MinGW-w64, then run:

```powershell
cmake -S . -B build/windows -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build/windows --parallel
ctest --test-dir build/windows --output-on-failure
```

### Ubuntu and Debian

```bash
sudo apt install build-essential cmake pkg-config python3 libcurl4-openssl-dev \
  libsecret-1-dev libglib2.0-dev libayatana-appindicator3-dev
cmake -S . -B build/linux -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux --parallel
ctest --test-dir build/linux --output-on-failure
```

For a headless Linux build, omit `libayatana-appindicator3-dev` and configure with
`-DAUDIOSYNC_TRAY=OFF`. Use `watch --no-tray` with that build.

Python 3 enables the CLI integration tests. They use isolated configuration directories and simulated
transfers. Use `-DBUILD_TESTING=OFF` to build only the application. Release CI also tests the Linux tray.

## Project structure

- `src/` contains shared sync, configuration, organization, progress, JSON, and logging modules.
- `src/cli/` contains commands and terminal output.
- `src/adapters/` contains the remote-source contract, registry, and implementations.
- `src/platform/` contains shared platform interfaces.
- `src/platform/windows/` and `src/platform/linux/` contain native implementations selected by CMake.
- `tests/` contains unit and integration tests.
- `packaging/` contains OS-specific installers and desktop assets.
- `scripts/` contains build and release automation.

Headers live beside their modules, and includes resolve from `src/`. Matching Windows and Linux
implementations use the same filename. Shared code and test fixtures use the same internal library and
compiler settings.

## Editor setup

CMake exports `compile_commands.json` and copies it to the source root for clangd. Configure and build once
if an include such as `platform/devices.h` is reported missing, then restart the language server.

The most recent build with `AUDIOSYNC_EDITOR_CONFIG=ON` controls the editor configuration. Disable it for
secondary builds with `-DAUDIOSYNC_EDITOR_CONFIG=OFF`. Refresh the active configuration with:

```text
cmake --build build/windows --target editor_config
```

## Packaging

Use the same release command on Windows from MSYS2 or Git Bash, and on Linux or WSL:

```bash
bash scripts/build-release.sh 1.2.3
```

Windows builds create an installer and portable archive. Linux and WSL builds create `.deb` and `.tar.gz`
packages. Release CI also creates AppImages for x86-64 and ARM64. Pass a CMake generator as the second
argument when needed, such as `Ninja`. Generated files stay under `build/` and `dist/`.

Linux users can install the latest AppImage without root access:

```bash
curl -fsSL https://raw.githubusercontent.com/PXR05/audiosync/main/install.sh | bash
```

Pass a version such as `0.1.0` to install a specific release, or pass `--uninstall` to remove it.
