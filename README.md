# 音频监听转发（AudioMonitor）

AudioMonitor 把一个输出设备上正在播放的声音转发到另一个输出设备。界面可选择监听源与目标、调节 0%–200% 音量，并支持系统托盘、设置持久化和设备热插拔。

支持 Windows 和 Linux。源设备与目标设备必须不同；程序会拒绝相同设备，避免形成反馈回路。

## 后端架构

| 平台 | 监听源 | 实时路径 |
| --- | --- | --- |
| Windows | WASAPI 回环捕获（`AUDCLNT_STREAMFLAGS_LOOPBACK`） | 捕获线程 → 有界严格 SPSC RingBuffer → WASAPI 共享模式渲染线程 |
| Linux | 所选 PipeWire `Audio/Sink` 的 monitor 输出 | monitor 端口 → 单个原生 `pw_filter` 的输入/输出端口 → 目标 sink |

### Linux：原生 PipeWire 过滤节点

Linux 后端直接连接 `libpipewire-0.3`，不经过兼容音频 API。它通过 PipeWire registry 枚举音频 sink，优先使用 `object.serial` 作为稳定选择 ID，缺失时回退到 `node.name`，并从 metadata 跟踪默认 sink。设备变化通过 Qt queued signal 送回界面，不从实时线程访问 UI。

监听会创建一个带立体声 `FL`/`FR` 输入和输出端口的 `pw_filter`，并自动建立以下图连接：

```text
所选源 sink 的 monitor 输出
    → pw_filter 输入端口
    → 同一个 PipeWire 实时 process cycle 内应用音量
    → pw_filter 输出端口
    → 所选目标 sink
```

处理阶段使用 float32 立体声；需要的设备格式转换和重采样由 PipeWire 图协商。实时回调只读取当前 cycle 的大小和映射缓冲区、复制或缩放样本，并在本 cycle 没有有效输入时写静音。回调不分配内存、不加锁、不记录日志、不执行文件 I/O，也不修改 PipeWire 图。

Linux 路径没有应用层 PCM RingBuffer，也不会保存缺失 cycle 之前的历史音频。延迟主要由协商后的 PipeWire graph rate/quantum、节点处理延迟、链接状态和设备延迟决定。

### Windows：跨设备时钟的有界 SPSC 队列

WASAPI 捕获端和渲染端运行在不同线程及设备时钟边界，因此 Windows 保留有界严格 SPSC RingBuffer。队列在源 mix format 已知后按实际采样率创建；生产者只发布写游标，消费者只修改读游标。

队列满时生产者丢弃本次无法容纳的新帧并发布 discontinuity generation，不移动读游标，也不覆盖消费者可能正在读取的存储区。消费者观察到新 generation 后用自己的读游标清掉旧队列区间，等待新音频继续播放，避免长时间渲染停顿后播放陈旧历史数据。

## 与 OBS Studio 的关系

本项目参考 OBS 音频监听在实时处理、生命周期和故障恢复方面的原则，但 Linux 后端架构并非从 OBS 的 PipeWire 音频代码复制而来，也不宣称与 OBS 完全一致。

当前上游 OBS 的 Linux 音频监听后端仍使用 [PulseAudio API](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/pulse/pulseaudio-output.c)，在 PipeWire 桌面上通常由 `pipewire-pulse` 提供兼容服务。本项目的 Linux 后端使用原生 PipeWire API；Windows 路径可对照 OBS 的 [WASAPI monitoring backend](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/win32/wasapi-output.c)。PipeWire 过滤节点模型见 [`pw_filter` 文档](https://docs.pipewire.org/group__pw__filter.html) 和 [DSP filter tutorial](https://docs.pipewire.org/page_tutorial7.html)。

## 构建

通用依赖为 CMake ≥ 3.16、C++17 编译器和 Qt 6 Widgets。Linux 另外需要 `pkg-config` 与原生 PipeWire 0.3 开发文件。

### Debian / Ubuntu

```bash
sudo apt install build-essential cmake ninja-build pkg-config qt6-base-dev libpipewire-0.3-dev
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/audiomonitor
```

### Fedora

```bash
sudo dnf install gcc-c++ cmake ninja-build pkgconf-pkg-config qt6-qtbase-devel pipewire-devel
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Arch Linux

```bash
sudo pacman -S --needed base-devel cmake ninja pkgconf qt6-base pipewire
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### NixOS / Nix

仓库根目录提供 `default.nix`、`flake.nix`、`shell.nix` 和辅助脚本 `build.sh`。`wrapQtAppsHook` 会注入 Qt 平台插件所需运行库路径。

```bash
nix build .#audiomonitor
./result/bin/audiomonitor

# 开发环境
nix develop       # flake
# 或 nix-shell    # classic
```

经典构建也可使用：

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
```

### Windows（MSVC）

```powershell
cmake -B build -DCMAKE_PREFIX_PATH=C:\Qt\6.x\msvc2022_64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

分发时可用 `windeployqt build\Release\audiomonitor.exe` 收集 Qt 运行库。GitHub Actions 的 Windows ZIP 将程序和所需 Qt/MSVC DLL 放在同一个 `audiomonitor-windows` 顶层目录中；请完整解压后运行。

## 使用

1. 选择监听源和另一个转发目标。相同设备会被拒绝。
2. 点击「开始监听」；状态栏显示当前路由。
3. 关闭窗口会在支持系统托盘的桌面环境中转入后台。
4. 所选设备移除或后端断开时，当前 session 会停止并报告一次有用错误，随后刷新设备列表。

命令行辅助模式：

```bash
audiomonitor --list-devices
audiomonitor --forward <源id> <目标id> [音量]
audiomonitor --smoke-test 3000
```

调试 dump 参数仍可用于支持它们的后端：

```bash
audiomonitor --forward A B --dump-capture /tmp/cap.raw
audiomonitor --forward A B --dump-playback /tmp/play.raw
audiomonitor --forward A B --dump-callbacks /tmp/cb.txt
```

原生 PipeWire 后端明确禁用这三个 dump 功能并返回错误；它不会在实时 process 回调里打开、写入或关闭文件。Windows 后端的 dump 行为保持不变。

## 延迟与状态

Linux 后端会通过受支持的 PipeWire 属性请求合理的低延迟 graph quantum，但请求值不是测量结果，也不会强制全局 quantum。可用时，状态信息来自运行时协商的 graph rate、quantum、process latency 以及节点/链接状态。项目不发布一个仅由请求参数推算出的固定“实际延迟”。

Windows 状态分别看待 RingBuffer 当前占用量和物理容量、WASAPI 捕获缓冲容量、渲染 padding、设备/stream latency。最大容量之和不等同于实际音频年龄。

## 已知限制

- Linux 需要可用的 PipeWire audio graph 和 session manager；没有 monitor 输出的 sink 不能作为监听源。
- 原生 PipeWire 后端不支持调试音频 dump，原因见上文。
- Linux 系统托盘依赖 StatusNotifier/AppIndicator；部分 Wayland 桌面不提供托盘支持。
- Windows 监听源被独占模式占用时，WASAPI loopback 可能无法启动。
- 设备必须已连接并处于激活状态。不同硬件、驱动和 PipeWire/WASAPI 配置下的延迟需要在运行环境中测量。

## 目录结构

```text
src/
  main.cpp                入口与命令行辅助模式
  mainwindow.{h,cpp}      主窗口、系统托盘和设置持久化
  appicon.{h,cpp}         程序化绘制图标
  core/
    audiorouter.{h,cpp}   引擎抽象接口与工厂
    ringbuffer.h          Windows WASAPI 路径的有界 SPSC RingBuffer
    audiorouter_win.*     Windows WASAPI 实现
    audiorouter_linux.*   Linux 原生 PipeWire pw_filter 实现
```

## 分发与 CI

发布工作流生成 `.deb`、`.rpm`、`.tar.gz`、`.AppImage`、Arch `.pkg.tar.zst` 和 PKGBUILD。通用包及 AppImage 仍依赖主机提供正在运行的 PipeWire 服务；包格式不会替代系统音频图。发行版依赖和兼容性说明见 [.github/PACKAGING.md](.github/PACKAGING.md)。

- `.github/workflows/build.yml` 在 Ubuntu 24.04（Qt 6 + `libpipewire-0.3-dev`）和 Windows 上构建并运行测试。
- `.github/workflows/release.yml` 在标签构建时运行 Linux 测试并生成发布包。
