{
  description = "AudioMonitor — 跨平台输出设备音频监听转发工具";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = { self, nixpkgs }:
    let
      inherit (nixpkgs) lib;
      systems = [ "x86_64-linux" "aarch64-linux" ];
      forAllSystems = f: lib.genAttrs systems f;
    in
    {
      packages = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          audiomonitor = pkgs.callPackage ./default.nix { };
          default = pkgs.callPackage ./default.nix { };
        });

      # nix develop / nix shell：完整开发环境（Qt6 + PipeWire + cmake）
      devShells = forAllSystems (system:
        let pkgs = nixpkgs.legacyPackages.${system};
        in {
          default = pkgs.mkShell {
            inputsFrom = [ (pkgs.callPackage ./default.nix { }) ];
            packages = [
              pkgs.cmake
              pkgs.pkg-config
            ];
          };
        });
    };
}
