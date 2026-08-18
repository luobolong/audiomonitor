# Linux Packaging

Linux release packaging is driven by `.github/workflows/release.yml`. The
workflow builds and tests one Ubuntu 24.04 CMake tree, stages the install tree
once, and reuses it for the binary package formats. The Arch job builds the
repository `packaging/PKGBUILD` with `makepkg` in an Arch container so the
resulting package has a native `.pkg.tar.zst` index.

## Release artifacts

Numeric `vX.Y.Z` tags publish:

- `audiomonitor_<version>_amd64.deb` for Debian-family systems.
- `audiomonitor-<version>-1.x86_64.rpm` for RPM-family systems.
- `audiomonitor-<version>-linux-x86_64.tar.gz` for manual installation.
- `audiomonitor-<version>-x86_64.AppImage` with Qt bundled by linuxdeploy.
- `audiomonitor-<version>-1-x86_64.pkg.tar.zst` and the matching `PKGBUILD`.
- `SHA256SUMS` for the published files.

Manual workflow runs require an explicit numeric `X.Y.Z` package version.
For those runs, the Arch job packages the checked-out ref through a local
source archive; tagged releases validate the checked-in tag-based `PKGBUILD`.

Nix is source-based rather than a release asset. The flake and classic
`default.nix` expression are checked in CI and can be built with:

```sh
nix build .#audiomonitor
# or
nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
```

## Package dependencies

The Debian package derives shared-library dependencies with
`dpkg-shlibdeps` when available. The RPM declares `qt6-qtbase` and
`pipewire-libs` and also uses RPM's automatic ELF dependency generator. The
Arch recipe declares `qt6-base` and `pipewire`. All Linux packages require a
running PipeWire graph and session manager; installing a package does not
provide an audio server.

The AppImage bundles Qt and the application resources but deliberately leaves
the PipeWire client library to the host. This keeps it compatible with the
host's PipeWire graph and session manager. A system with FUSE support or
`APPIMAGE_EXTRACT_AND_RUN=1` is required to run it.

Qt translation catalogs are compiled from `translations/audiomonitor_en.ts`
and `translations/audiomonitor_zh_CN.ts` by CMake. Qt Linguist tools are a
required build dependency, and the catalogs are embedded in the application.
The staging script also copies the generated `.qm` files into
`/usr/share/audiomonitor/translations` for package inspection.

## Local package builds

Build and test the application first:

Install CMake, Ninja, a C++17 compiler, Qt 6 Widgets and Linguist tools,
`pkg-config`, and the PipeWire 0.3 development package using your
distribution's package manager.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr
cmake --build build
ctest --test-dir build --output-on-failure
```

Then create Debian, RPM, and tarball packages with the shared script:

```sh
packaging/package-linux.sh 1.0.0 build dist all
```

The script requires `dpkg-deb` (and preferably `dpkg-shlibdeps`) for Debian,
`rpmbuild` for RPM, and standard GNU tar. AppImage creation is kept in the
release workflow because linuxdeploy supplies the Qt bundling tool. To build
the Arch package from a tagged checkout:

```sh
sed -i 's/^pkgver=.*/pkgver=1.0.0/' packaging/PKGBUILD
makepkg --cleanbuild --syncdeps
```

`packaging/stage-linux.sh` is the common staging entry point. It installs via
CMake, adds the desktop entry, documentation, icon, and any generated
translation files. `packaging/test-packages.sh` performs local metadata and
archive checks without requiring audio hardware.

## Runtime requirements

Binary packages are built on Ubuntu 24.04 and therefore target x86_64 Linux
systems with compatible glibc and Qt/PipeWire ABI versions. The source-based
Arch and Nix recipes use the target distribution's dependencies. None of the
package checks exercise live routing; that requires two distinct output sinks,
monitor ports, and a running PipeWire session manager.
