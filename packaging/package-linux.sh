#!/usr/bin/env bash
# Build binary Linux packages from one CMake build and staged install tree.

set -euo pipefail

usage() {
    printf 'Usage: %s <version> <build-dir> <output-dir> [deb|rpm|tar|all]\n' "$0" >&2
    exit 2
}

[[ $# -ge 3 && $# -le 4 ]] || usage

VERSION=${1#v}
if [[ ! "$VERSION" =~ ^[0-9]+([.][0-9]+)*([+~].*)?$ ]]; then
    VERSION=0.0.0
fi
BUILD_DIR=$2
OUTPUT_DIR=$3
FORMAT=${4:-all}
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/audiomonitor-package.XXXXXX")
STAGE_DIR="$WORK_DIR/stage"

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

mkdir -p "$OUTPUT_DIR"
"$SCRIPT_DIR/stage-linux.sh" "$BUILD_DIR" "$STAGE_DIR" /usr

write_deb() {
    command -v dpkg-deb >/dev/null 2>&1 || {
        printf 'dpkg-deb is required for Debian packages.\n' >&2
        return 1
    }

    local package_root="$WORK_DIR/deb"
    local package_file="$OUTPUT_DIR/audiomonitor_${VERSION}_amd64.deb"
    mkdir -p "$package_root/DEBIAN"
    cp -a "$STAGE_DIR/." "$package_root/"

    local dependencies
    dependencies='libc6 (>= 2.39), libqt6core6t64, libqt6gui6, libqt6widgets6, libpipewire-0.3-0t64'
    if command -v dpkg-shlibdeps >/dev/null 2>&1; then
        local shlibs
        shlibs=$(cd "$package_root" && dpkg-shlibdeps -O \
            -e "$package_root/usr/bin/audiomonitor" 2>/dev/null || true)
        if [[ "$shlibs" == shlibs:Depends=* ]]; then
            dependencies=${shlibs#shlibs:Depends=}
        fi
    fi

    cat > "$package_root/DEBIAN/control" << EOF
Package: audiomonitor
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: amd64
Depends: ${dependencies}
Maintainer: AudioMonitor contributors <luobolong@users.noreply.github.com>
Homepage: https://github.com/luobolong/audiomonitor
Description: Audio monitoring and forwarding tool
 AudioMonitor forwards audio from one output device to another in real time.
 The Linux backend uses a native PipeWire filter node and requires a running
 PipeWire graph and session manager.
EOF

    dpkg-deb --build --root-owner-group "$package_root" "$package_file"
}

write_rpm() {
    command -v rpmbuild >/dev/null 2>&1 || {
        printf 'rpmbuild is required for RPM packages.\n' >&2
        return 1
    }

    local topdir="$WORK_DIR/rpmbuild"
    mkdir -p "$topdir"/{BUILD,RPMS,SOURCES,SPECS,SRPMS}

    cat > "$topdir/SPECS/audiomonitor.spec" << EOF
Name:           audiomonitor
Version:        ${VERSION}
Release:        1%{?dist}
Summary:        Audio monitoring and forwarding tool using native PipeWire
License:        MIT
URL:            https://github.com/luobolong/audiomonitor
Requires:       qt6-qtbase >= 6.2
Requires:       pipewire-libs

%description
AudioMonitor forwards audio from one output device to another in real time.
The Linux backend uses a native PipeWire filter node.

%install
rm -rf %{buildroot}
mkdir -p %{buildroot}
cp -a "$STAGE_DIR/." %{buildroot}/

%files
%license /usr/share/doc/audiomonitor/LICENSE
%doc /usr/share/doc/audiomonitor/README.md
%{_bindir}/audiomonitor
%{_datadir}/applications/audiomonitor.desktop
%{_datadir}/icons/hicolor/16x16/apps/audiomonitor.png
%{_datadir}/icons/hicolor/24x24/apps/audiomonitor.png
%{_datadir}/icons/hicolor/32x32/apps/audiomonitor.png
%{_datadir}/icons/hicolor/48x48/apps/audiomonitor.png
%{_datadir}/icons/hicolor/64x64/apps/audiomonitor.png
%{_datadir}/icons/hicolor/128x128/apps/audiomonitor.png
%{_datadir}/icons/hicolor/256x256/apps/audiomonitor.png
%dir %{_datadir}/audiomonitor
%dir %{_datadir}/audiomonitor/translations
%{_datadir}/audiomonitor/translations/audiomonitor_en.qm
%{_datadir}/audiomonitor/translations/audiomonitor_zh_CN.qm

%changelog
* $(LC_ALL=C date '+%a %b %d %Y') AudioMonitor contributors <luobolong@users.noreply.github.com> - ${VERSION}-1
- Release version ${VERSION}
EOF

    rpmbuild --define "_topdir $topdir" \
        --define "_prefix /usr" \
        --define "_bindir /usr/bin" \
        --define "_datadir /usr/share" \
        --define "_builddir $topdir/BUILD" \
        --define "_buildrootdir $topdir/BUILDROOT" \
        --define "_rpmdir $topdir/RPMS" \
        --define "_srcrpmdir $topdir/SRPMS" \
        --define "_sourcedir $topdir/SOURCES" \
        -bb "$topdir/SPECS/audiomonitor.spec"
    cp "$topdir/RPMS"/*/*.rpm "$OUTPUT_DIR/"
}

write_tarball() {
    local package_dir="$WORK_DIR/audiomonitor-${VERSION}-linux-x86_64"
    local archive="$OUTPUT_DIR/audiomonitor-${VERSION}-linux-x86_64.tar.gz"
    mkdir -p "$package_dir"
    cp -a "$STAGE_DIR/." "$package_dir/"
    install -Dm755 "$SCRIPT_DIR/install-tarball.sh" "$package_dir/install.sh"
    cat > "$package_dir/README.txt" << EOF
AudioMonitor ${VERSION} binary archive

Run ./install.sh [PREFIX] from this directory (default: /usr/local).
The host must provide Qt 6 runtime libraries, libpipewire-0.3, and a running
PipeWire graph/session manager. The AppImage is the more self-contained option.
EOF
    tar --sort=name --mtime='UTC 1970-01-01' --owner=0 --group=0 \
        --numeric-owner -czf "$archive" -C "$WORK_DIR" \
        "$(basename "$package_dir")"
}

case "$FORMAT" in
    deb) write_deb ;;
    rpm) write_rpm ;;
    tar|tar.gz) write_tarball ;;
    all)
        write_deb
        write_rpm
        write_tarball
        ;;
    *) usage ;;
esac
