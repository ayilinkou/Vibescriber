#!/usr/bin/env bash
set -euo pipefail

if (( $# != 3 )); then
    echo "usage: build-appimage.sh BUILD_DIR OUTPUT_DIR LINUXDEPLOY" >&2
    exit 2
fi

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=$(realpath "$1")
output_dir=$(realpath -m "$2")
linuxdeploy=$(realpath "$3")
version=$("$repo_dir/build/Release/vibescriber" --version)
version=${version#Vibescriber }
if [[ ! $version =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]]; then
    echo "unexpected application version: $version" >&2
    exit 1
fi

mkdir -p "$output_dir"
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
appdir="$work_dir/AppDir"
cmake --install "$build_dir" --config Release --prefix "$appdir/usr"
rsvg-convert -w 128 -h 128 "$repo_dir/packaging/vibescriber.svg" \
    > "$work_dir/vibescriber.png"

(
    cd "$work_dir"
    APPIMAGE_EXTRACT_AND_RUN=1 NO_STRIP=1 "$linuxdeploy" \
        --appdir "$appdir" \
        --desktop-file "$repo_dir/packaging/vibescriber.desktop" \
        --icon-file "$work_dir/vibescriber.png" \
        --output appimage
)

shopt -s nullglob
images=("$work_dir"/*.AppImage)
if (( ${#images[@]} != 1 )); then
    echo "expected one AppImage, found ${#images[@]}" >&2
    exit 1
fi
destination="$output_dir/Vibescriber-v$version-linux-x86_64.AppImage"
mv -- "${images[0]}" "$destination"
chmod +x "$destination"
echo "$destination"
