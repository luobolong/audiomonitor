#!/usr/bin/env bash
# 本机构建脚本（NixOS/Nix 环境）：自动定位 nix store 中的 Qt6 与 PipeWire 开发包。
# 其他平台请直接使用 cmake（参见 README.md）。
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
PIPEWIRE_DEV=""
for p in /nix/store/*-pipewire-*-dev; do
    if [ -f "$p/lib/pkgconfig/libpipewire-0.3.pc" ] \
       && [ -d "$p/include/pipewire-0.3" ]; then
        PIPEWIRE_DEV="$p"
        break
    fi
done

# Qt6Gui 依赖 OpenGL：定位 libglvnd 的 dev（头文件/gl.pc）与 runtime（libGL.so）
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

if [ -z "$QT_PREFIX" ] || [ -z "$PIPEWIRE_DEV" ] || [ -z "$GL_DEV" ] || [ -z "$GL_RUN" ]; then
    echo "错误：未在 /nix/store 中找到 Qt6 / PipeWire / libglvnd 开发包。" >&2
    echo "请先运行: nix-shell -p qt6.qtbase pipewire libglvnd" >&2
    exit 1
fi

export PKG_CONFIG_PATH="$QT_PREFIX/lib/pkgconfig:$PIPEWIRE_DEV/lib/pkgconfig:$GL_DEV/lib/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
export CMAKE_PREFIX_PATH="$QT_PREFIX${CMAKE_PREFIX_PATH:+:$CMAKE_PREFIX_PATH}"
export CMAKE_INCLUDE_PATH="$GL_DEV/include${CMAKE_INCLUDE_PATH:+:$CMAKE_INCLUDE_PATH}"
export CMAKE_LIBRARY_PATH="$GL_DEV/lib:$GL_RUN/lib${CMAKE_LIBRARY_PATH:+:$CMAKE_LIBRARY_PATH}"
export PATH="$QT_PREFIX/bin:$PATH"

BUILD_DIR="${1:-build}"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo
echo "构建完成: $BUILD_DIR/audiomonitor"
echo "列出设备: $BUILD_DIR/audiomonitor --list-devices"
echo "运行:     $BUILD_DIR/audiomonitor"
