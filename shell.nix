# Classic nix-shell development environment for non-flake users:
#   nix-shell            # Enter a Qt 6/PipeWire/CMake build environment.
#   cmake -B build && cmake --build build
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  inputsFrom = [ (pkgs.callPackage ./default.nix { }) ];
  packages = [
    pkgs.cmake
    pkgs.pkg-config
  ];
}
