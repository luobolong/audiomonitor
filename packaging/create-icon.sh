#!/usr/bin/env bash
# Helper script to create a placeholder icon for packaging.
# The actual application icon is generated programmatically at runtime
# via src/appicon.cpp, but package formats require a static icon file.

set -euo pipefail

OUTPUT_DIR="${1:-.}"
ICON_PATH="$OUTPUT_DIR/audiomonitor.png"

mkdir -p "$OUTPUT_DIR"

# Create a simple 256x256 PNG icon with ImageMagick
# (fallback: create a 1x1 transparent PNG if ImageMagick is not available)

if command -v convert >/dev/null 2>&1 && convert -size 256x256 xc:transparent \
    -fill '#3498db' \
    -draw 'circle 128,128 128,64' \
    -fill '#2ecc71' \
    -draw 'rectangle 64,96 192,160' \
    -fill 'white' \
    -draw 'path "M 80,128 Q 112,80 144,128 T 208,128"' \
    "$ICON_PATH"; then
    :
elif command -v convert >/dev/null 2>&1; then
    # ImageMagick may reject drawing operations under a restrictive policy.
    rm -f "$ICON_PATH"
    printf 'ImageMagick could not create the icon; using the fallback.\n' >&2
    printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82' > "$ICON_PATH"
else
    # Fallback: 1x1 transparent PNG (minimal valid PNG)
    printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82' > "$ICON_PATH"
fi

if [[ ! -s "$ICON_PATH" ]]; then
    printf '\x89PNG\r\n\x1a\n\x00\x00\x00\rIHDR\x00\x00\x00\x01\x00\x00\x00\x01\x08\x06\x00\x00\x00\x1f\x15\xc4\x89\x00\x00\x00\nIDATx\x9cc\x00\x01\x00\x00\x05\x00\x01\r\n-\xb4\x00\x00\x00\x00IEND\xaeB`\x82' > "$ICON_PATH"
fi

if [[ -s "$ICON_PATH" ]]; then
    echo "Icon created: $ICON_PATH"
fi
