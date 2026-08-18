# Linux Packaging Guide

This document describes the automated Linux packaging system for AudioMonitor.

## Overview

The project uses two separate GitHub Actions workflows:

1. **`build.yml`** - Continuous integration build that runs on every push/PR
   - Builds for Linux (Ubuntu) and Windows
   - Uploads basic build artifacts
   - Fast feedback for development

2. **`release.yml`** - Release packaging that runs on Git tags
   - Creates distribution-specific packages
   - Uploads to GitHub Releases
   - Generates release notes

## Triggering a Release

To create a new release with all packaging formats:

```bash
# Tag the release
git tag -a v1.0.1 -m "Release version 1.0.1"
git push origin v1.0.1
```

This will automatically:
1. Build the application from source
2. Create packages for all supported distributions
3. Upload them as GitHub Release assets

## Package Formats

### Debian/Ubuntu (.deb)
- **Target**: Debian, Ubuntu, Linux Mint, Pop!_OS, Elementary OS
- **Structure**: Standard Debian package with control file
- **Dependencies**: Declared via `Depends:` field
- **Installation**: `sudo dpkg -i audiomonitor_*.deb && sudo apt-get install -f`

### Fedora/RHEL (.rpm)
- **Target**: Fedora, RHEL, CentOS, Rocky Linux, AlmaLinux
- **Structure**: RPM package built from spec file
- **Dependencies**: Declared via `Requires:` field
- **Installation**: `sudo dnf install audiomonitor-*.rpm`

### Generic Linux (.tar.gz)
- **Target**: Any Linux distribution
- **Structure**: Simple directory archive with install script
- **Dependencies**: User must install manually
- **Installation**: `sudo ./install.sh` (installs to /usr/local by default)

### AppImage (.AppImage)
- **Target**: Any Linux distribution with FUSE
- **Structure**: Portable bundle containing the application and selected user-space libraries
- **Dependencies**: A running host PipeWire graph/session manager is still required
- **Installation**: `chmod +x audiomonitor-*.AppImage && ./audiomonitor-*.AppImage`

### Arch Linux (.pkg.tar.zst + PKGBUILD)
- **Target**: Arch Linux, Manjaro, EndeavourOS
- **Structure**: Arch package + PKGBUILD for AUR
- **Dependencies**: Declared in PKGBUILD
- **Installation**: `sudo pacman -U audiomonitor-*.pkg.tar.zst`

### NixOS (Flake)
- **Target**: NixOS, Nix users on any distro
- **Structure**: Nix expression (already in repo: `flake.nix`, `default.nix`)
- **Dependencies**: Declared in Nix expression
- **Installation**: `nix build github:luobolong/audiomonitor/v1.0.0`

## Package Contents

All packages include:
- Binary: `audiomonitor` (compiled with Qt6 and native `libpipewire-0.3` support)
- Desktop entry: `/usr/share/applications/audiomonitor.desktop`
- Documentation: README.md, LICENSE

## Reproducible Builds

The workflow follows these principles for reproducibility:

1. **Separation of Concerns**: 
   - Build stage compiles the binary once
   - Packaging stages consume the same binary artifact
   - No recompilation per package format

2. **Explicit Dependencies**:
   - Qt 6.2+ (Core, Gui, Widgets)
   - Native PipeWire client library (`libpipewire-0.3`)
   - System libraries (libc6, libstdc++)

3. **Consistent Environment**:
   - Ubuntu 24.04 for build and Debian-package metadata
   - Container for Arch package
   - Fixed tool versions where possible

## Customizing Packages

### Updating Version Number

Version is automatically extracted from the Git tag (`v1.0.0` → `1.0.0`).

### Changing Dependencies

Edit the dependency declarations in `release.yml`:

- **DEB**: `Depends:` field in control file
- **RPM**: `Requires:` field in spec file
- **Arch**: `depends=()` array in PKGBUILD
- **Nix**: `buildInputs` in `default.nix`

### Adding Desktop Integration

Desktop files, icons, and metadata are defined in each packaging job.
To update, edit `packaging/audiomonitor.desktop` and
`packaging/create-icon.sh`; the release workflow consumes those files.

## Testing Packages Locally

### Test DEB package:
```bash
docker run --rm -v $PWD:/work -w /work ubuntu:latest bash -c \
  "apt-get update && apt-get install -y ./audiomonitor_*.deb"
```

### Test RPM package:
```bash
docker run --rm -v $PWD:/work -w /work fedora:latest bash -c \
  "dnf install -y ./audiomonitor-*.rpm"
```

### Test Arch package:
```bash
docker run --rm -v $PWD:/work -w /work archlinux:latest bash -c \
  "pacman -Syu --noconfirm && pacman -U --noconfirm ./audiomonitor-*.pkg.tar.zst"
```

### Test AppImage:
```bash
chmod +x audiomonitor-*.AppImage
./audiomonitor-*.AppImage --smoke-test 500
```

Package installation and process-start checks do not validate live device
routing. A routing test additionally needs two distinct PipeWire sinks with
monitor/input ports and a running session manager.

## Manual Release Process

If you need to create packages manually:

```bash
# 1. Build the application
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
DESTDIR=$PWD/install-root cmake --install build

# 2. Create DEB package
# ... (follow steps in release.yml)

# 3. Create RPM package
# ... (follow steps in release.yml)
```

## Troubleshooting

### AppImage fails to build
- AppImage normally uses FUSE; `APPIMAGE_EXTRACT_AND_RUN=1` is an alternative
- Check that Qt6 is properly installed
- Verify `linuxdeploy` can find Qt plugin

### RPM dependencies not found
- Adjust `Requires:` field to match target distribution
- Fedora uses `qt6-qtbase`, RHEL might differ

### Package size too large
- Check bundled dependencies
- Consider static linking strategy
- Verify no debug symbols are included

## Future Enhancements

Potential additions to the packaging system:

- [ ] Flatpak package
- [ ] Snap package
- [ ] Checksums (SHA256) for all artifacts
- [ ] GPG signing of packages
- [ ] AUR (Arch User Repository) automatic publishing
- [ ] Homebrew formula for macOS
- [ ] Validation tests for each package format
- [ ] Multi-architecture builds (arm64, armhf)
