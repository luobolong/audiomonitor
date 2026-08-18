#!/usr/bin/env bash
# Install an extracted AudioMonitor binary archive.

set -euo pipefail

PREFIX=${1:-/usr/local}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [[ ! -x "$SCRIPT_DIR/usr/bin/audiomonitor" ]]; then
    printf 'Run this script from the extracted package directory.\n' >&2
    exit 1
fi

install -Dm755 "$SCRIPT_DIR/usr/bin/audiomonitor" \
    "$PREFIX/bin/audiomonitor"

if [[ -f "$SCRIPT_DIR/usr/share/applications/audiomonitor.desktop" ]]; then
    install -Dm644 "$SCRIPT_DIR/usr/share/applications/audiomonitor.desktop" \
        "$PREFIX/share/applications/audiomonitor.desktop"
fi

if [[ -d "$SCRIPT_DIR/usr/share/icons" ]]; then
    while IFS= read -r -d '' icon; do
        relative=${icon#"$SCRIPT_DIR/usr/"}
        install -Dm644 "$icon" "$PREFIX/$relative"
    done < <(find "$SCRIPT_DIR/usr/share/icons" -type f -print0)
fi

if [[ -d "$SCRIPT_DIR/usr/share/audiomonitor" ]]; then
    while IFS= read -r -d '' data_file; do
        relative=${data_file#"$SCRIPT_DIR/usr/"}
        install -Dm644 "$data_file" "$PREFIX/$relative"
    done < <(find "$SCRIPT_DIR/usr/share/audiomonitor" -type f -print0)
fi

if [[ -d "$SCRIPT_DIR/usr/share/doc/audiomonitor" ]]; then
    while IFS= read -r -d '' doc_file; do
        relative=${doc_file#"$SCRIPT_DIR/usr/"}
        install -Dm644 "$doc_file" "$PREFIX/$relative"
    done < <(find "$SCRIPT_DIR/usr/share/doc/audiomonitor" -type f -print0)
fi

printf 'Installed AudioMonitor under %s\n' "$PREFIX"
