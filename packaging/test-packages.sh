#!/usr/bin/env bash
# Build and inspect Linux package metadata without requiring audio hardware.

set -euo pipefail

FORMAT=${1:-all}
BUILD_DIR=${2:-build}
OUTPUT_DIR=$(realpath -m "${3:-dist}")
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
PROJECT_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)
PROJECT_PARENT=$(dirname "$PROJECT_ROOT")
PROJECT_GRANDPARENT=$(dirname "$PROJECT_PARENT")

cd "$PROJECT_ROOT"

case "$OUTPUT_DIR" in
    ""|/|.)
        printf 'Refusing to replace an unsafe output path: %s\n' "$OUTPUT_DIR" >&2
        exit 2
        ;;
esac

if [[ "$OUTPUT_DIR" == "$PROJECT_ROOT" || "$OUTPUT_DIR" == "$PROJECT_PARENT" \
    || "$OUTPUT_DIR" == "$PROJECT_GRANDPARENT" ]]; then
    printf 'Refusing to replace a workspace path: %s\n' "$OUTPUT_DIR" >&2
    exit 2
fi

if [[ ! -x "$BUILD_DIR/audiomonitor" ]]; then
    cmake -S . -B "$BUILD_DIR" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_INSTALL_PREFIX=/usr
    cmake --build "$BUILD_DIR" --parallel
    ctest --test-dir "$BUILD_DIR" --output-on-failure
fi

if [[ -e "$OUTPUT_DIR" ]]; then
    if [[ ! -d "$OUTPUT_DIR" ]] \
        || [[ -n "$(find "$OUTPUT_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
        printf 'Output path must not exist or must be an empty directory: %s\n' \
            "$OUTPUT_DIR" >&2
        exit 2
    fi
    rmdir "$OUTPUT_DIR"
fi
mkdir -p "$OUTPUT_DIR"

case "$FORMAT" in
    deb|rpm|tar|tar.gz|all)
        "$SCRIPT_DIR/package-linux.sh" 1.0.0 "$BUILD_DIR" "$OUTPUT_DIR" "$FORMAT"
        ;;
    arch)
        if command -v makepkg >/dev/null 2>&1; then
            makepkg --printsrcinfo -p "$SCRIPT_DIR/PKGBUILD" > "$OUTPUT_DIR/.SRCINFO"
            printf 'Arch PKGBUILD metadata is valid.\n'
        else
            printf 'makepkg is unavailable; run this check in an Arch container.\n' >&2
        fi
        ;;
    appimage)
        printf 'AppImage creation runs in the release workflow with linuxdeploy.\n'
        ;;
    *)
        printf 'Usage: %s [deb|rpm|tar|arch|appimage|all] [build-dir] [output-dir]\n' "$0" >&2
        exit 2
        ;;
esac

if [[ "$FORMAT" == deb || "$FORMAT" == all ]]; then
    command -v dpkg-deb >/dev/null 2>&1 && dpkg-deb --info "$OUTPUT_DIR"/*.deb
fi
if [[ "$FORMAT" == rpm || "$FORMAT" == all ]]; then
    command -v rpm >/dev/null 2>&1 && rpm -qip "$OUTPUT_DIR"/*.rpm
fi
if [[ "$FORMAT" == tar || "$FORMAT" == tar.gz || "$FORMAT" == all ]]; then
    tar -tzf "$OUTPUT_DIR"/*.tar.gz >/dev/null
fi

printf 'Package checks completed.\n'
