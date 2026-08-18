# 经典 nix-shell 开发环境（非 flake 用户）：
#   nix-shell            # 进入带 Qt6/PipeWire/cmake 的构建环境
#   cmake -B build && cmake --build build
{ pkgs ? import <nixpkgs> { } }:

pkgs.mkShell {
  inputsFrom = [ (pkgs.callPackage ./default.nix { }) ];
  packages = [
    pkgs.cmake
    pkgs.pkg-config
  ];
}
