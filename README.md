# AudioMonitor

简体中文 | [English](README_EN.md)

AudioMonitor 是一个面向 Windows 和 Linux 的实时音频监听转发工具。它捕获一个输出设备正在播放的声音，并低延迟地转发到另一个输出设备。

适合以下场景：

- 同时监听两个耳机、音箱或声卡；
- 把系统播放声转发到采集卡或外部设备；
- 测试不同音频设备之间的路由与延迟；
- 在不启动完整录制或直播软件的情况下进行轻量返听。

> 源设备和目标设备必须不同。程序会拒绝相同设备，避免形成音频反馈回路。

## 功能

- 选择监听源和转发目标；
- 0%–500% 音量调节，支持常用档位磁吸和数值输入；
- Windows WASAPI 回环捕获与事件驱动播放；
- Linux 原生 PipeWire 图连接，不经过 PulseAudio 兼容层；
- 针对独立硬件时钟的水位控制和轻量漂移补偿；
- 设备热插拔检测与自动重连；
- 系统托盘后台运行；
- 设备选择、音量和界面语言持久化；
- English / 简体中文界面；
- Windows、Linux 自动构建、测试与发布打包。

## 工作原理

| 平台 | 监听源 | 实时路径 |
| --- | --- | --- |
| Windows | WASAPI 回环捕获 | 捕获线程 → 严格 SPSC RingBuffer → 直接/自适应读取器 → WASAPI 共享模式渲染线程 |
| Linux | PipeWire sink 的 monitor 输出 | monitor 端口 → 原生 `pw_filter` → 目标 sink |

### Windows：WASAPI 与跨设备时钟

Windows 后端在两个独立的工作线程中运行：

```text
源输出设备
    → WASAPI loopback capture
    → 格式转换与立体声下混
    → 有界 SPSC RingBuffer
    → 直接读取或水位与时钟漂移控制
    → WASAPI shared-mode render
    → 目标输出设备
```

主要设计：

- 捕获端使用源设备的 mix format，支持 float32 以及 16/24/32-bit PCM；
- 内部队列使用交错 float32 立体声帧；
- 捕获线程只发布写游标，渲染线程只更新读游标；
- 实时路径不分配内存、不加锁、不记录日志，也不执行文件 I/O；
- 两个线程在进入音频循环前注册 MMCSS，优先使用 `Pro Audio` 类别；
- 渲染端根据 WASAPI padding 只填写当前可用空间；
- 目标设备需要的采样率和格式转换由 Windows 音频引擎完成。

源设备和目标设备通常拥有不同的硬件时钟。即使二者都标称 48 kHz，实际速率也会有微小偏差。固定 RingBuffer 只能推迟欠载或溢出，不能消除这种长期漂移。

Windows 托盘菜单提供两种互斥的监听模式，选择会被保存；监听中切换会自动重启当前音频链路：

- **低延迟**：采用原来的直接读取实现。每次 WASAPI 请求都直接读取 RingBuffer，不预先建立目标水位，通常具有更低的队列延迟，但更容易受调度抖动、短时欠载和长期时钟漂移影响；
- **稳定**：采用当前的自适应实现，也是默认模式。它维护短目标水位，以一部分延迟换取更稳定的跨设备监听。

稳定模式的渲染端会：

- 基础目标约为 20 ms，并结合 WASAPI 渲染缓冲大小和安全余量调整；
- 使用约 2 ms 的迟滞区间，避免在目标附近频繁修正；
- 源端稍快时，一个回调最多多消费 1 帧；
- 源端稍慢时，一个回调最多少消费 1 帧；
- 输入块通过线性映射生成 WASAPI 请求的准确帧数；
- 启动或欠载恢复时先重新建立安全余量，再恢复声音；
- 音量变化在一个输出块内平滑过渡，降低点击声。

队列物理容量为 50 ms，但它不是固定监听延迟。低延迟模式不主动维持队列水位；稳定模式的基础目标约为 20 ms。实际队列水位、WASAPI 缓冲、设备驱动和硬件都会共同影响端到端延迟。

发生捕获中断或队列溢出时，生产者发布 discontinuity generation。消费者随后丢弃中断前积压的旧音频并重新缓冲，避免恢复后继续播放过时内容。

### Linux：原生 PipeWire

Linux 后端直接使用 `libpipewire-0.3`：

```text
所选源 sink 的 monitor 输出
    → pw_filter 输入端口（FL / FR）
    → 同一 PipeWire process cycle 内应用音量
    → pw_filter 输出端口（FL / FR）
    → 所选目标 sink
```

主要设计：

- 从 PipeWire registry 枚举 `Audio/Sink` 节点；
- 优先使用 `object.serial` 生成稳定设备 ID，缺失时回退到 `node.name`；
- 从 metadata 跟踪默认输出设备；
- 创建一个同时拥有输入和输出端口的原生 `pw_filter`；
- 设备格式转换和重采样由 PipeWire 图协商；
- 没有应用层 PCM RingBuffer，不保存缺失 cycle 之前的历史音频；
- 实时回调只映射缓冲区、复制或缩放样本，并在输入无效时写入静音；
- 设备变化通过 Qt queued signal 返回界面，实时线程不访问 UI。

后端会请求合理的低延迟 graph quantum，但最终 quantum 和实际延迟由 PipeWire、session manager、设备及当前音频图共同决定。

## 下载与使用

可以从项目的 [Releases](https://github.com/luobolong/audiomonitor/releases) 页面下载构建产物。

Windows 发布版提供：

- 便携版 ZIP；
- MSI 安装包。

Linux 发布版提供：

- DEB；
- RPM；
- AppImage；
- 通用 `tar.gz`。

Linux 通用包和 AppImage 仍要求主机提供正在运行的 PipeWire 服务和 session manager。

### 图形界面

1. 在“监听源”中选择当前正在播放声音的输出设备；
2. 在“转发目标”中选择另一个输出设备；
3. 设置监听音量；
4. Windows 用户可在托盘菜单的“监听模式”中选择“低延迟”或“稳定”；
5. 点击“开始监听”；
6. 关闭窗口后，程序会在支持系统托盘的桌面环境中继续运行。

设备被移除或音频服务暂时不可用时，当前 session 会停止。界面会保留实际运行 session 使用的设备 ID，并在设备重新出现后尝试自动连接。

### 命令行辅助模式

```text
audiomonitor --list-devices
audiomonitor --forward <源设备ID> <目标设备ID> [音量]
audiomonitor --smoke-test <毫秒>
```

示例：

```powershell
audiomonitor --list-devices
audiomonitor --forward "{source-device-id}" "{target-device-id}" 1.0
```

`--forward` 会持续运行，直到进程被终止。音量使用倍数表示：`1.0` 为 100%，`2.0` 为 200%。

## 从源码构建

### 通用依赖

- CMake 3.16 或更高版本；
- 支持 C++17 的编译器；
- Qt 5 或 Qt 6，包含 Widgets 与 LinguistTools；
- Windows：Windows 10/11 SDK、MSVC 或兼容工具链；
- Linux：`pkg-config` 和 PipeWire 0.3 开发文件。

### Debian / Ubuntu

```bash
sudo apt install \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-tools-dev qt6-l10n-tools \
  libpipewire-0.3-dev

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/audiomonitor
```

### Fedora

```bash
sudo dnf install \
  gcc-c++ cmake ninja-build pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qttools-devel pipewire-devel

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Arch Linux

```bash
sudo pacman -S --needed \
  base-devel cmake ninja pkgconf qt6-base qt6-tools pipewire

cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows（MSVC）

首先安装 Qt 和 Visual Studio 2022 C++ 工具，然后指定 Qt 的 MSVC 路径：

```powershell
cmake -S . -B build `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

如果 Qt 位于仓库的 `qt/6.8.0/msvc2022_64`，也可以使用预设：

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

分发便携版时，可以运行：

```powershell
windeployqt build\Release\audiomonitor.exe
```

### Nix / NixOS

```bash
nix build .#audiomonitor
./result/bin/audiomonitor

# 开发环境
nix develop

# 经典 nix-shell
nix-shell
```

也可以使用：

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
```

## 测试

启用 `BUILD_TESTING` 后，项目包含以下测试：

- `realtime_audio`：PipeWire 实时立体声处理；
- `ringbuffer`：SPSC 队列、并发、回绕、欠载和 discontinuity；
- `adaptive_audio_buffer`：启动水位、快慢时钟、再缓冲和音量渐变；
- `mainwindow`：界面 session 状态与自动重连。

运行全部测试：

```bash
ctest --test-dir build --output-on-failure
```

多配置 Windows 构建需要额外指定配置：

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## 延迟说明

AudioMonitor 不把缓冲区容量宣称为实测延迟。

Windows 的端到端延迟可能包含：

- 源设备及其 Windows 音频引擎缓冲；
- WASAPI loopback capture 周期；
- 应用队列当前水位；
- 目标 WASAPI padding 和 stream latency；
- 目标设备驱动与硬件缓冲。

如果只追求最低名义延迟，可选择“低延迟”；如果更在意不同设备长期运行时的连续性，保留默认的“稳定”更合适。模式名称描述的是缓冲策略，不代表所有设备上的实测延迟保证。

Linux 的端到端延迟可能包含：

- PipeWire graph rate 和 quantum；
- filter 节点处理周期；
- source/target link 状态；
- 两端设备及驱动延迟。

需要准确结果时，应在目标硬件上使用回环线缆或时间戳工具测量，不能简单相加配置中的容量数值。

## 与 OBS Studio 的关系

本项目参考 OBS Studio 音频监听实现中的实时处理、设备生命周期和故障恢复原则，但两者处理的输入链路不同：

- OBS 的 Windows monitoring backend 接收 OBS 音频引擎已经混合和重采样的数据，再写入 WASAPI；
- AudioMonitor 直接跨两个输出设备转发音频，因此必须额外处理独立设备时钟和应用层队列；
- Linux 后端使用原生 PipeWire `pw_filter`，而不是复制 OBS 的 PulseAudio monitoring backend。

参考：

- [OBS Studio WASAPI monitoring backend](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/win32/wasapi-output.c)
- [OBS Studio PulseAudio monitoring backend](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/pulse/pulseaudio-output.c)
- [PipeWire `pw_filter` API](https://docs.pipewire.org/group__pw__filter.html)
- [PipeWire DSP filter tutorial](https://docs.pipewire.org/page_tutorial7.html)
- [Microsoft WASAPI loopback recording](https://learn.microsoft.com/windows/win32/coreaudio/loopback-recording)

## 已知限制

- Windows WASAPI loopback 只能在共享模式下工作；
- 源设备被应用程序独占时，回环捕获可能无法启动；
- 当前 Windows 内部处理固定为立体声，多声道源会下混；
- 不同设备、驱动和系统音频配置下的延迟与稳定性需要实机验证；
- Linux 需要可用的 PipeWire 服务和 session manager；
- Linux 源 sink 必须提供 FL/FR monitor 端口，目标 sink 必须提供 FL/FR 输入端口；
- Linux 系统托盘依赖桌面提供 StatusNotifier/AppIndicator 支持；
- 当前界面不显示实测延迟、队列水位或漂移修正统计。

## 项目结构

```text
src/
  main.cpp                       程序入口与命令行辅助模式
  mainwindow.{h,cpp}             主窗口、托盘、设置与重连
  appicon.{h,cpp}                应用图标
  core/
    audiorouter.{h,cpp}          后端抽象接口与工厂
    audiorouter_win.{h,cpp}      Windows WASAPI 后端
    audiorouter_linux.{h,cpp}    Linux PipeWire 后端
    ringbuffer.h                 有界严格 SPSC 音频队列
    adaptive_audio_buffer.h      Windows 水位、漂移与音量控制
    realtime_audio.h             Linux 实时立体声处理
tests/
  ringbuffer_test.cpp
  adaptive_audio_buffer_test.cpp
  realtime_audio_test.cpp
  mainwindow_test.cpp
translations/
  audiomonitor_en.ts
  audiomonitor_zh_CN.ts
packaging/
  Linux 打包与安装脚本
```

## 发布与持续集成

GitHub Actions 会：

- 在 Windows 和 Ubuntu 上构建项目；
- 运行自动化测试；
- 编译 Qt 翻译目录；
- 为版本标签生成 Windows ZIP/MSI；
- 生成 Linux DEB、RPM、AppImage 和通用压缩包；
- 为发布附件生成 SHA-256 校验文件。

详细打包说明见 [`.github/PACKAGING.md`](.github/PACKAGING.md)。

## 许可证

本项目使用 [MIT License](LICENSE)。
