#!/usr/bin/env bash
# Stage the files shared by the Linux package formats.

set -euo pipefail

usage() {
    printf 'Usage: %s <build-dir> <stage-dir> [prefix]\n' "$0" >&2
    exit 2
}

[[ $# -ge 2 && $# -le 3 ]] || usage

BUILD_DIR=$1
STAGE_DIR=$(realpath -m "$2")
PREFIX=${3:-/usr}
PROJECT_ROOT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
PROJECT_PARENT=$(dirname "$PROJECT_ROOT")
PROJECT_GRANDPARENT=$(dirname "$PROJECT_PARENT")

case "$STAGE_DIR" in
    ""|/|.)
        printf 'Refusing to replace an unsafe staging path: %s\n' "$STAGE_DIR" >&2
        exit 2
        ;;
esac

if [[ "$STAGE_DIR" == "$PROJECT_ROOT" || "$STAGE_DIR" == "$PROJECT_PARENT" \
    || "$STAGE_DIR" == "$PROJECT_GRANDPARENT" ]]; then
    printf 'Refusing to replace a workspace path: %s\n' "$STAGE_DIR" >&2
    exit 2
fi

if [[ ! -x "$BUILD_DIR/audiomonitor" ]]; then
    printf 'Build executable not found: %s/audiomonitor\n' "$BUILD_DIR" >&2
    exit 1
fi

if [[ -e "$STAGE_DIR" ]]; then
    if [[ ! -d "$STAGE_DIR" ]] \
        || [[ -n "$(find "$STAGE_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]]; then
        printf 'Staging path must not exist or must be an empty directory: %s\n' \
            "$STAGE_DIR" >&2
        exit 2
    fi
    rmdir "$STAGE_DIR"
fi
mkdir -p "$STAGE_DIR"

# Use the install rules from CMake so all package formats receive the same files.
DESTDIR="$STAGE_DIR" cmake --install "$BUILD_DIR" --prefix "$PREFIX"

install -Dm644 "$PROJECT_ROOT/packaging/audiomonitor.desktop" \
    "$STAGE_DIR$PREFIX/share/applications/audiomonitor.desktop"
install -Dm644 "$PROJECT_ROOT/README.md" \
    "$STAGE_DIR$PREFIX/share/doc/audiomonitor/README.md"
install -Dm644 "$PROJECT_ROOT/LICENSE" \
    "$STAGE_DIR$PREFIX/share/doc/audiomonitor/LICENSE"

# Package metadata expects an icon.  The application also draws its icon at
# runtime, so keep generation optional for minimal build environments.
ICON_DIR="$STAGE_DIR$PREFIX/share/icons/hicolor/256x256/apps"
mkdir -p "$ICON_DIR"
if command -v convert >/dev/null 2>&1; then
    "$PROJECT_ROOT/packaging/create-icon.sh" "$ICON_DIR"
else
    printf 'ImageMagick not available; using the generated fallback icon.\n' >&2
    "$PROJECT_ROOT/packaging/create-icon.sh" "$ICON_DIR"
fi

# Qt may either embed translations in the executable or install generated
# .qm files.  Copy generated files when present so both layouts remain usable.
TRANSLATION_DIR="$STAGE_DIR$PREFIX/share/audiomonitor/translations"
while IFS= read -r -d '' qm_file; do
    install -Dm644 "$qm_file" "$TRANSLATION_DIR/$(basename "$qm_file")"
done < <(find "$BUILD_DIR" -type f \( \
    -name 'audiomonitor_en.qm' -o -name 'audiomonitor_zh_CN.qm' \
    \) -print0)

printf 'Staged Linux tree at %s\n' "$STAGE_DIR"
