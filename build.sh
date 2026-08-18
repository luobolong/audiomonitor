#!/usr/bin/env bash
# Local build helper for NixOS/Nix environments. It locates Qt 6 and
# PipeWire development packages in the Nix store.
# On other platforms, invoke CMake directly (see README.md).
set -euo pipefail
cd "$(dirname "$0")"

QT_PREFIX=""
for p in /nix/store/*-qtbase-6.* /nix/store/*-qtbase-6.*-*; do
    if [ -d "$p/lib/cmake/Qt6Widgets" ] && [ -d "$p/lib/cmake/Qt6Gui" ] \
       && [ -d "$p/include/QtWidgets" ]; then
        QT_PREFIX="$p"
        break
    fi
done
QT_TOOLS_PREFIX=""
for p in /nix/store/*-qttools-6.* /nix/store/*-qttools-6.*-*; do
    if [ -f "$p/lib/cmake/Qt6LinguistTools/Qt6LinguistToolsConfig.cmake" ]; then
        QT_TOOLS_PREFIX="$p"
        break
    fi
done
PIPEWIRE_DEV=""
for p in /nix/store/*-pipewire-*-dev; do
    if [ -f "$p/lib/pkgconfig/libpipewire-0.3.pc" ] \
       && [ -d "$p/include/pipewire-0.3" ]; then
        PIPEWIRE_DEV="$p"
        break
    fi
done

# Qt6Gui depends on OpenGL. Locate libglvnd development headers and runtime.
GL_DEV=""
for p in /nix/store/*-libglvnd-*-dev; do
    if [ -f "$p/include/GL/gl.h" ] && [ -d "$p/lib/pkgconfig" ]; then
        GL_DEV="$p"
        break
    fi
done
GL_RUN=""
for p in /nix/store/*-libglvnd-*; do
    if [ -f "$p/lib/libGL.so" ] && file -L "$p/lib/libGL.so" 2>/dev/null | grep -q "64-bit"; then
        GL_RUN="$p"
        break
    fi
done

if [ -z "$QT_PREFIX" ] || [ -z "$QT_TOOLS_PREFIX" ] || [ -z "$PIPEWIRE_DEV" ] \
    || [ -z "$GL_DEV" ] || [ -z "$GL_RUN" ]; then
    echo "Qt 6, Qt LinguistTools, PipeWire, or libglvnd development packages were not found in /nix/store." >&2
    echo "Enter a shell first: nix-shell -p qt6.qtbase qt6.qttools pipewire libglvnd" >&2
    exit 1
fi

export PKG_CONFIG_PATH="$QT_PREFIX/lib/pkgconfig:$PIPEWIRE_DEV/lib/pkgconfig:$GL_DEV/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CMAKE_PREFIX_PATH="$QT_PREFIX:$QT_TOOLS_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export CMAKE_INCLUDE_PATH="$GL_DEV/include${CMAKE_INCLUDE_PATH:+:$CMAKE_INCLUDE_PATH}"
export CMAKE_LIBRARY_PATH="$GL_DEV/lib:$GL_RUN/lib${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"
export PATH="$QT_PREFIX/bin:$PATH"

BUILD_DIR="${1:-build}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "Build complete: $BUILD_DIR/audiomonitor"
echo "List devices:  $BUILD_DIR/audiomonitor --list-devices"
echo "Run:           $BUILD_DIR/audiomonitor"
