# AudioMonitor 的 Nix 打包表达式。
# 用法（经典模式，无需 flake）:
#   nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
# 或通过 flake:
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
    # 剔除本地构建产物，避免污染 nix store
    filter = name: type:
      !(type == "directory"
        && (baseNameOf name == "build" || baseNameOf name == "dist"));
  };

  nativeBuildInputs = [
    cmake
    pkg-config
    qt6.wrapQtAppsHook # 自动包装二进制：注入 Qt 平台插件等运行库路径
  ];

  buildInputs = [
    qt6.qtbase      # QtWidgets/QtGui/QtCore
    pipewire        # Linux 原生 PipeWire 客户端库与开发文件
    libglvnd        # Qt6Gui 依赖的 OpenGL（FindOpenGL 需要头文件与 libGL.so）
  ];

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    # 显式给出 OpenGL 的查找路径，避免 FindOpenGL 在纯净环境中落空
    "-DCMAKE_INCLUDE_PATH=${lib.getDev libglvnd}/include"
    "-DCMAKE_LIBRARY_PATH=${lib.getLib libglvnd}/lib"
  ];

  meta = with lib; {
    description = "把一个输出设备正在播放的音频实时转发到另一个输出设备";
    license = licenses.mit;
    mainProgram = "audiomonitor";
    platforms = platforms.linux;
    maintainers = [ ];
  };
})
