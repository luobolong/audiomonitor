# Nix package expression for AudioMonitor.
# Classic usage without flakes:
#   nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
# Or with the flake:
#   nix build .#audiomonitor
{
  lib
, stdenv
, cmake
, pkg-config
, qt6
, pipewire
, libglvnd
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "audiomonitor";
  version = "1.0.0";

  src = lib.cleanSourceWith {
    src = ./.;
    # Exclude local build products from the Nix source tree.
    filter = name: type:
      !(type == "directory"
        && (baseNameOf name == "build" || baseNameOf name == "dist"));
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    qt6.qttools
    qt6.wrapQtAppsHook
  ];

  buildInputs = [
    qt6.qtbase
    pipewire
    libglvnd
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    # Help FindOpenGL locate libglvnd in minimal Nix environments.
    "-DCMAKE_INCLUDE_PATH=${lib.getDev libglvnd}/include"
    "-DCMAKE_LIBRARY_PATH=${lib.getLib libglvnd}/lib"
  ];

  doCheck = true;

  postInstall = ''
    if [[ "$sourceRoot" = /* ]]; then
      packageSource="$sourceRoot"
    else
      packageSource="$NIX_BUILD_TOP/$sourceRoot"
    fi
    install -Dm644 "$packageSource/packaging/audiomonitor.desktop" \
      $out/share/applications/audiomonitor.desktop
    install -Dm644 "$packageSource/README.md" $out/share/doc/audiomonitor/README.md
    install -Dm644 "$packageSource/LICENSE" $out/share/licenses/audiomonitor/LICENSE
    mkdir -p $out/share/icons/hicolor/256x256/apps
    bash "$packageSource/packaging/create-icon.sh" $out/share/icons/hicolor/256x256/apps
  '';

  meta = with lib; {
    description = "Forward audio from one output device to another in real time";
    license = licenses.mit;
    mainProgram = "audiomonitor";
    platforms = platforms.linux;
    maintainers = [ ];
  };
})
