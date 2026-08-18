{
  description = "AudioMonitor: cross-platform output-device audio monitor";

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

      # nix develop / nix shell: complete Qt 6, PipeWire, and CMake environment.
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
