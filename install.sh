#!/usr/bin/env bash
set -euo pipefail

repo="PXR05/audiosync"
data_dir="${XDG_DATA_HOME:-$HOME/.local/share}/audiosync"
bin_dir="${XDG_BIN_HOME:-$HOME/.local/bin}"
app="$data_dir/audiosync.AppImage"
link="$bin_dir/audiosync"

usage() {
    echo "Usage: install.sh [VERSION] | --uninstall"
}

if [[ "${1:-}" == --uninstall ]]; then
    [[ ! -L "$link" || "$(readlink "$link")" != "$app" ]] || rm "$link"
    rm -f "$app"
    rmdir "$data_dir" 2>/dev/null || true
    echo "AudioSync uninstalled"
    exit
elif [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    usage
    exit
elif (($# > 1)); then
    usage >&2
    exit 2
fi

[[ "$(uname -s)" == Linux ]] || { echo "error: Linux is required" >&2; exit 1; }
case "$(uname -m)" in
    x86_64|amd64) arch=x86_64 ;;
    aarch64|arm64) arch=aarch64 ;;
    *) echo "error: only x86-64 and ARM64 are supported" >&2; exit 1 ;;
esac
for command in curl sha256sum install; do
    command -v "$command" >/dev/null || { echo "error: $command is required" >&2; exit 1; }
done

tag="${1:-}"
if [[ -z "$tag" ]]; then
    url="$(curl -fsSL -o /dev/null -w '%{url_effective}' "https://github.com/$repo/releases/latest")"
    tag="${url##*/}"
else
    tag="v${tag#v}"
fi
version="${tag#v}"
[[ "$version" =~ ^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$ ]] ||
    { echo "error: invalid version: $version" >&2; exit 1; }

asset="AudioSync-$version-$arch.AppImage"
sums="AudioSync-$version-linux-$arch-SHA256SUMS.txt"
base="https://github.com/$repo/releases/download/$tag"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

curl -fL --progress-bar -o "$tmp/$asset" "$base/$asset"
curl -fsSL -o "$tmp/$sums" "$base/$sums"
(cd "$tmp" && grep "  $asset$" "$sums" | sha256sum -c -)

mkdir -p "$data_dir" "$bin_dir"
install -m 755 "$tmp/$asset" "$app"
ln -sfn "$app" "$link"

echo "AudioSync $version installed at $link"
case ":$PATH:" in
    *":$bin_dir:"*) ;;
    *) echo "Add $bin_dir to PATH to run: audiosync" ;;
esac
