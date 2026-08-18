#!/bin/bash
# Test packaging workflow locally
# Usage: ./test-packages.sh [deb|rpm|arch|all]

set -euo pipefail

BUILD_TYPE="${1:-all}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

cd "$PROJECT_ROOT"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${GREEN}[INFO]${NC} $*"
}

log_warn() {
    echo -e "${YELLOW}[WARN]${NC} $*"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $*"
}

# Check if docker is available
if ! command -v docker >/dev/null 2>&1; then
    log_error "Docker is required to test packages"
    exit 1
fi

# Build the application first
build_app() {
    log_info "Building application..."

    if [ ! -d "build" ]; then
        docker run --rm -v "$PWD:/work" -w /work ubuntu:24.04 bash -c "
            apt-get update -qq
            apt-get install -y -qq build-essential cmake ninja-build pkg-config qt6-base-dev libpipewire-0.3-dev
            cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
            cmake --build build
            ctest --test-dir build --output-on-failure
        "
    else
        log_info "Using existing build directory"
    fi

    # Create install root
    mkdir -p install-root
    docker run --rm -v "$PWD:/work" -w /work ubuntu:24.04 bash -c "
        apt-get update -qq
        apt-get install -y -qq cmake
        DESTDIR=/work/install-root cmake --install build
    "

    log_info "Build complete: build/audiomonitor"
}

# Test DEB package
test_deb() {
    log_info "Testing DEB package creation and installation..."

    VERSION="${APP_VERSION:-1.0.0}"
    PACKAGE_NAME="audiomonitor_${VERSION}_amd64"

    # Create package structure
    mkdir -p "$PACKAGE_NAME/DEBIAN"
    mkdir -p "$PACKAGE_NAME/usr/bin"
    mkdir -p "$PACKAGE_NAME/usr/share/applications"
    mkdir -p "$PACKAGE_NAME/usr/share/doc/audiomonitor"

    cp build/audiomonitor "$PACKAGE_NAME/usr/bin/"
    chmod 755 "$PACKAGE_NAME/usr/bin/audiomonitor"

    cp packaging/audiomonitor.desktop "$PACKAGE_NAME/usr/share/applications/"
    cp README.md LICENSE "$PACKAGE_NAME/usr/share/doc/audiomonitor/"

    cat > "$PACKAGE_NAME/DEBIAN/control" << EOF
Package: audiomonitor
Version: ${VERSION}
Section: sound
Priority: optional
Architecture: amd64
Depends: libqt6core6t64 (>= 6.2), libqt6gui6 (>= 6.2), libqt6widgets6 (>= 6.2), libpipewire-0.3-0t64
Maintainer: luobolong
Description: Audio monitoring and forwarding tool
 AudioMonitor forwards audio output from one device to another in real-time.
Homepage: https://github.com/luobolong/audiomonitor
EOF

    docker run --rm -v "$PWD:/work" -w /work ubuntu:24.04 bash -c "
        dpkg-deb --build --root-owner-group '$PACKAGE_NAME'
    "

    log_info "Testing DEB installation..."
    docker run --rm -v "$PWD:/work" -w /work ubuntu:24.04 bash -c "
        apt-get update -qq
        apt-get install -y -qq ./'$PACKAGE_NAME.deb' || apt-get install -f -y
        audiomonitor --version || audiomonitor --help
        dpkg -l | grep audiomonitor
    " && log_info "✓ DEB package works!" || log_error "✗ DEB package failed"

    rm -rf "$PACKAGE_NAME"
}

# Test RPM package
test_rpm() {
    log_info "Testing RPM package creation and installation..."

    VERSION="${APP_VERSION:-1.0.0}"

    docker run --rm -v "$PWD:/work" -w /work fedora:latest bash -c "
        dnf install -y -q rpm-build

        mkdir -p ~/rpmbuild/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
        mkdir -p ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/bin
        mkdir -p ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/share/applications
        mkdir -p ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/share/doc/audiomonitor

        cp /work/build/audiomonitor ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/bin/
        cp /work/packaging/audiomonitor.desktop ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/share/applications/
        cp /work/README.md /work/LICENSE ~/rpmbuild/BUILDROOT/audiomonitor-${VERSION}-1.x86_64/usr/share/doc/audiomonitor/

        cat > ~/rpmbuild/SPECS/audiomonitor.spec << 'SPEC_EOF'
Name:           audiomonitor
Version:        ${VERSION}
Release:        1%{?dist}
Summary:        Audio monitoring and forwarding tool
License:        MIT
URL:            https://github.com/luobolong/audiomonitor
Requires:       qt6-qtbase >= 6.2, pipewire-libs

%description
AudioMonitor forwards audio output from one device to another in real-time.

%files
%license /usr/share/doc/audiomonitor/LICENSE
%doc /usr/share/doc/audiomonitor/README.md
/usr/bin/audiomonitor
/usr/share/applications/audiomonitor.desktop

%changelog
* $(date '+%a %b %d %Y') luobolong - ${VERSION}-1
- Release version ${VERSION}
SPEC_EOF

        rpmbuild --target x86_64 -bb ~/rpmbuild/SPECS/audiomonitor.spec
        cp ~/rpmbuild/RPMS/x86_64/*.rpm /work/

        dnf install -y /work/audiomonitor-${VERSION}-1.*.rpm
        audiomonitor --version || audiomonitor --help
        rpm -qa | grep audiomonitor
    " && log_info "✓ RPM package works!" || log_error "✗ RPM package failed"
}

# Test Arch package
test_arch() {
    log_info "Testing Arch package creation and installation..."

    VERSION="${APP_VERSION:-1.0.0}"

    docker run --rm -v "$PWD:/work" -w /work archlinux:latest bash -c "
        pacman -Syu --noconfirm
        pacman -S --noconfirm base-devel

        mkdir -p pkg/usr/bin
        mkdir -p pkg/usr/share/applications
        mkdir -p pkg/usr/share/licenses/audiomonitor

        cp /work/build/audiomonitor pkg/usr/bin/
        cp /work/packaging/audiomonitor.desktop pkg/usr/share/applications/
        cp /work/LICENSE pkg/usr/share/licenses/audiomonitor/

        cat > pkg/.PKGINFO << PKGINFO_EOF
pkgname = audiomonitor
pkgver = ${VERSION}-1
pkgdesc = Audio monitoring and forwarding tool
url = https://github.com/luobolong/audiomonitor
builddate = \$(date +%s)
packager = luobolong
size = \$(du -sb pkg | cut -f1)
arch = x86_64
license = MIT
depend = qt6-base
depend = pipewire
PKGINFO_EOF

        cd pkg
        env LANG=C bsdtar -czf ../audiomonitor-${VERSION}-1-x86_64.pkg.tar.zst .PKGINFO *
        cd ..

        pacman -U --noconfirm audiomonitor-${VERSION}-1-x86_64.pkg.tar.zst
        audiomonitor --version || audiomonitor --help
        pacman -Q audiomonitor
    " && log_info "✓ Arch package works!" || log_error "✗ Arch package failed"
}

# Main execution
main() {
    log_info "AudioMonitor Package Testing Script"
    log_info "===================================="

    # Build the app if needed
    if [ ! -f "build/audiomonitor" ]; then
        build_app
    else
        log_info "Using existing build"
    fi

    case "$BUILD_TYPE" in
        deb)
            test_deb
            ;;
        rpm)
            test_rpm
            ;;
        arch)
            test_arch
            ;;
        all)
            test_deb
            echo ""
            test_rpm
            echo ""
            test_arch
            ;;
        *)
            log_error "Unknown build type: $BUILD_TYPE"
            echo "Usage: $0 [deb|rpm|arch|all]"
            exit 1
            ;;
    esac

    log_info "Testing complete!"
}

main
