#!/usr/bin/env bash
set -euo pipefail

version="${1:-0.1.0}"
generator="${2:-${CMAKE_GENERATOR:-}}"
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dist="$root/dist"

if [[ ! "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]]; then
    echo "error: version must look like 1.2.3" >&2
    exit 2
fi
IFS=. read -r major minor patch <<<"$version"
for part in "$major" "$minor" "$patch"; do
    if ((part > 65535)); then
        echo "error: version components must be at most 65535" >&2
        exit 2
    fi
done

case "$(uname -s)" in
    MINGW*|MSYS*|CYGWIN*) platform=windows ;;
    Linux*) platform=linux ;;
    *) echo "error: AudioSync supports Windows and Linux" >&2; exit 2 ;;
esac
case "$(uname -m)" in
    x86_64|amd64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
    *) echo "error: unsupported architecture: $(uname -m)" >&2; exit 2 ;;
esac
if [[ -z "$generator" ]] && command -v ninja >/dev/null 2>&1; then
    generator=Ninja
elif [[ -z "$generator" && "$platform" == windows ]]; then
    generator="MinGW Makefiles"
fi

build="$root/build/release"
[[ "$platform" == linux ]] && build="$root/build/release-linux"
configure=(cmake -S "$root" -B "$build" -DCMAKE_BUILD_TYPE=Release
           "-DAUDIOSYNC_VERSION=$version" -DAUDIOSYNC_EDITOR_CONFIG=OFF)
[[ -n "$generator" ]] && configure+=(-G "$generator")

"${configure[@]}"
cmake --build "$build" --parallel
ctest --test-dir "$build" --output-on-failure
mkdir -p "$dist"

if [[ "$platform" == linux ]]; then
    cpack --config "$build/CPackConfig.cmake" -B "$dist"
    if [[ -n "${LINUXDEPLOY:-}" ]]; then
        appdir="$build/AppDir"
        appimage="$dist/AudioSync-$version-$arch.AppImage"
        rm -rf "$appdir"
        DESTDIR="$appdir" cmake --install "$build" --prefix /usr
        ARCH="$arch" LDAI_VERSION="$version" LDAI_OUTPUT="$appimage" DEPLOY_GTK_VERSION=3 \
            "$LINUXDEPLOY" --appdir "$appdir" --plugin gtk --output appimage \
            --desktop-file "$root/packaging/linux/audiosync.desktop" \
            --icon-file "$root/packaging/linux/audiosync.svg"
    fi
    (
        cd "$dist"
        files=("AudioSync-$version-linux-$arch.deb" "AudioSync-$version-linux-$arch.tar.gz")
        [[ -z "${LINUXDEPLOY:-}" ]] || files+=("AudioSync-$version-$arch.AppImage")
        sha256sum "${files[@]}" >"AudioSync-$version-linux-$arch-SHA256SUMS.txt"
    )
else
    portable="$root/build/portable-$version"
    rm -rf "$portable"
    mkdir -p "$portable/docs"
    cp "$build/audiosync.exe" "$portable/"
    cp "$root/README.md" "$portable/"
    cp "$root"/docs/*.md "$portable/docs/"

    iscc="${ISCC:-}"
    if [[ -z "$iscc" ]] && command -v ISCC.exe >/dev/null 2>&1; then
        iscc="$(command -v ISCC.exe)"
    fi
    for candidate in \
        "$root/.tools/inno/ISCC.exe" \
        "/c/Program Files (x86)/Inno Setup 6/ISCC.exe" \
        "/c/Users/${USERNAME:-}/AppData/Local/Programs/Inno Setup 6/ISCC.exe"; do
        [[ -n "$iscc" || ! -f "$candidate" ]] || iscc="$candidate"
    done
    if [[ -z "$iscc" ]]; then
        echo "error: install Inno Setup 6 or set ISCC to ISCC.exe" >&2
        exit 1
    fi

    to_windows() { command -v cygpath >/dev/null 2>&1 && cygpath -w "$1" || printf '%s\n' "$1"; }
    MSYS2_ARG_CONV_EXCL='*' "$iscc" "/DAppVersion=$version" "/DSourceDir=$(to_windows "$build")" \
        "/DOutputPath=$(to_windows "$dist")" "$(to_windows "$root/packaging/windows/audiosync.iss")"

    archive="$dist/AudioSync-$version-windows-x64.zip"
    rm -f "$archive"
    (cd "$portable" && cmake -E tar cf "$archive" --format=zip .)
    (
        cd "$dist"
        sha256sum "AudioSync-$version-windows-x64-setup.exe" \
            "AudioSync-$version-windows-x64.zip" \
            >"AudioSync-$version-SHA256SUMS.txt"
    )
fi

echo "Release $version is ready in $dist"
