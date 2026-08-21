# AudioMonitor

[简体中文](README.md) | English

AudioMonitor is a real-time audio monitoring and forwarding application for Windows and Linux. It captures the sound currently playing on one output device and forwards it to another output device with low latency.

Typical uses include:

- monitoring the same system audio through two headphones, speakers, or audio interfaces;
- forwarding playback to a capture card or an external device;
- testing audio routing and latency between devices;
- lightweight monitoring without running a full recording or streaming suite.

> The source and target must be different devices. AudioMonitor rejects identical endpoints to prevent an audio feedback loop.

## Features

- Selectable monitoring source and forwarding target;
- 0%–500% volume control with snap points and numeric input;
- WASAPI loopback capture and event-driven rendering on Windows;
- Native PipeWire graph integration on Linux, without the PulseAudio compatibility layer;
- Queue-level control and lightweight drift compensation for independent hardware clocks;
- Device hot-plug detection and automatic reconnect;
- Background operation through the system tray;
- Persistent device selections, volume, and UI language;
- English and Simplified Chinese UI;
- Automated Windows and Linux builds, tests, and release packages.

## Architecture

| Platform | Monitoring source | Real-time path |
| --- | --- | --- |
| Windows | WASAPI loopback capture | Capture thread → strict SPSC RingBuffer → direct/adaptive reader → shared-mode WASAPI render thread |
| Linux | PipeWire sink monitor output | Monitor ports → native `pw_filter` → target sink |

### Windows: WASAPI and independent device clocks

The Windows backend uses two dedicated worker threads:

```text
Source output device
    → WASAPI loopback capture
    → format conversion and stereo downmix
    → bounded SPSC RingBuffer
    → direct reading or queue-level and clock-drift control
    → shared-mode WASAPI rendering
    → target output device
```

Key properties:

- Capture uses the source endpoint mix format and accepts float32 plus 16/24/32-bit PCM;
- Internal queued audio is interleaved stereo float32;
- Only the capture thread publishes the write cursor, and only the render thread advances the read cursor;
- The real-time path performs no allocation, locking, logging, or file I/O;
- Both workers register with MMCSS before entering their audio loops, preferring the `Pro Audio` task category;
- The renderer uses WASAPI padding to fill only the currently available space;
- Sample-rate and format conversion required by the target endpoint is handled by the Windows audio engine.

The source and target endpoints normally use independent hardware clocks. Even if both are nominally 48 kHz, their real rates differ slightly. A fixed RingBuffer can only postpone an underrun or overrun; it cannot remove long-term drift.

The Windows tray menu provides two mutually exclusive monitoring modes. The selection is persisted, and changing it while monitoring automatically restarts the current audio path:

- **Low latency** uses the original direct-read implementation. It reads every WASAPI request directly from the RingBuffer without first building a target queue level. It normally has lower queueing latency but is more exposed to scheduling jitter, brief underruns, and long-term clock drift;
- **Stable** uses the current adaptive implementation and is the default. It maintains a short target level, trading some latency for more reliable monitoring between independent devices.

In stable mode, the render side:

- A base target of about 20 ms is adjusted for the WASAPI render buffer and safety headroom;
- About 2 ms of hysteresis prevents constant corrections near the target;
- If the source runs slightly fast, a callback consumes at most one extra frame;
- If the source runs slightly slow, a callback consumes at most one fewer frame;
- The input block is linearly mapped to the exact number of frames requested by WASAPI;
- Startup and underrun recovery rebuild the safety margin before audio resumes;
- Volume changes ramp over one output block to reduce clicks.

The physical queue capacity is 50 ms, but that is not a fixed monitoring delay. Low-latency mode does not deliberately maintain a queue level; stable mode has a base target of about 20 ms. Actual queue occupancy, WASAPI buffers, device drivers, and hardware all contribute to end-to-end latency.

When capture reports a discontinuity or the queue overruns, the producer publishes a discontinuity generation. The consumer then drops queued audio from before the interruption and re-buffers instead of replaying stale sound.

### Linux: native PipeWire

The Linux backend connects directly to `libpipewire-0.3`:

```text
Selected source sink monitor output
    → pw_filter input ports (FL / FR)
    → volume processing in the same PipeWire process cycle
    → pw_filter output ports (FL / FR)
    → selected target sink
```

Key properties:

- `Audio/Sink` nodes are enumerated from the PipeWire registry;
- `object.serial` is preferred for stable device IDs, with `node.name` as a fallback;
- The default output is tracked through PipeWire metadata;
- One native `pw_filter` owns both input and output ports;
- Device format conversion and resampling are negotiated by the PipeWire graph;
- There is no application-level PCM RingBuffer and no replay of audio from a missing process cycle;
- The real-time callback only maps buffers, copies or scales samples, and writes silence for invalid input;
- Device changes are delivered to the UI through queued Qt signals; the real-time thread never accesses UI objects.

The backend requests a reasonable low-latency graph quantum, but the final quantum and actual latency are controlled by PipeWire, the session manager, the devices, and the active graph.

## Download and usage

Prebuilt packages are available from the project [Releases](https://github.com/luobolong/audiomonitor/releases) page.

Windows releases include:

- Portable ZIP;
- MSI installer.

Linux releases include:

- DEB;
- RPM;
- AppImage;
- Generic `tar.gz`.

The generic Linux package and AppImage still require a running PipeWire service and session manager on the host.

### Graphical interface

1. Select the output device currently playing audio as the monitoring source;
2. Select a different output device as the forwarding target;
3. Set the monitoring volume;
4. On Windows, choose **Low latency** or **Stable** from **Monitoring mode** in the tray menu;
5. Click **Start monitoring**;
6. Closing the window keeps the application running when the desktop provides a system tray.

If a device is removed or the audio service becomes temporarily unavailable, the active session stops. The UI keeps the endpoint IDs actually used by that session and attempts to reconnect when both devices are available again.

### Command-line helper modes

```text
audiomonitor --list-devices
audiomonitor --forward <source-device-id> <target-device-id> [volume]
audiomonitor --smoke-test <milliseconds>
```

Examples:

```powershell
audiomonitor --list-devices
audiomonitor --forward "{source-device-id}" "{target-device-id}" 1.0
```

`--forward` runs until the process is terminated. Volume is expressed as a multiplier: `1.0` is 100%, and `2.0` is 200%.

## Building from source

### Common requirements

- CMake 3.16 or newer;
- A C++17 compiler;
- Qt 5 or Qt 6 with Widgets and LinguistTools;
- Windows: Windows 10/11 SDK and MSVC or a compatible toolchain;
- Linux: `pkg-config` and PipeWire 0.3 development files.

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

### Windows with MSVC

Install Qt and the Visual Studio 2022 C++ tools, then provide the path to the Qt MSVC package:

```powershell
cmake -S . -B build `
  -DCMAKE_PREFIX_PATH=C:\Qt\6.8.0\msvc2022_64

cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

If Qt is available at `qt/6.8.0/msvc2022_64` inside the repository, the included presets can be used:

```powershell
cmake --preset windows-msvc
cmake --build --preset windows-release
ctest --preset windows-release
```

For a portable deployment, run:

```powershell
windeployqt build\Release\audiomonitor.exe
```

### Nix / NixOS

```bash
nix build .#audiomonitor
./result/bin/audiomonitor

# Development shell
nix develop

# Classic nix-shell
nix-shell
```

The classic package expression is also available:

```bash
nix-build -E 'with import <nixpkgs> {}; callPackage ./default.nix {}'
```

## Tests

With `BUILD_TESTING` enabled, the project builds:

- `realtime_audio`: PipeWire real-time stereo processing;
- `ringbuffer`: SPSC queue behavior, concurrency, wraparound, underruns, and discontinuities;
- `adaptive_audio_buffer`: startup level, fast/slow clocks, rebuffering, and volume ramps;
- `mainwindow`: UI session state and reconnect behavior.

Run all tests with:

```bash
ctest --test-dir build --output-on-failure
```

Multi-configuration Windows builds also require the configuration:

```powershell
ctest --test-dir build -C Release --output-on-failure
```

## Latency notes

AudioMonitor does not treat a configured buffer capacity as measured latency.

Windows end-to-end latency can include:

- source endpoint and Windows audio-engine buffering;
- the WASAPI loopback capture period;
- the current application queue level;
- target WASAPI padding and stream latency;
- target driver and hardware buffers.

Choose **Low latency** when the lowest nominal latency is the only priority. Keep the default **Stable** mode when continuity across different devices matters more. These names describe buffering policies; they are not measured-latency guarantees for every device.

Linux end-to-end latency can include:

- the PipeWire graph rate and quantum;
- the filter-node processing cycle;
- source and target link state;
- driver and hardware latency at both endpoints.

For accurate results, measure on the target hardware with a physical loopback cable or timestamp-capable tooling. Adding configured buffer sizes together does not produce a reliable latency measurement.

## Relationship to OBS Studio

AudioMonitor follows principles from OBS Studio's monitoring backends around real-time processing, device lifetime, and fault recovery, but the input paths are different:

- The OBS Windows monitoring backend receives audio already mixed and resampled by the OBS audio engine before writing it to WASAPI;
- AudioMonitor forwards directly between two output endpoints, so it additionally needs an application queue and independent-clock handling;
- The Linux backend uses a native PipeWire `pw_filter` instead of copying the OBS PulseAudio monitoring backend.

References:

- [OBS Studio WASAPI monitoring backend](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/win32/wasapi-output.c)
- [OBS Studio PulseAudio monitoring backend](https://github.com/obsproject/obs-studio/blob/master/libobs/audio-monitoring/pulse/pulseaudio-output.c)
- [PipeWire `pw_filter` API](https://docs.pipewire.org/group__pw__filter.html)
- [PipeWire DSP filter tutorial](https://docs.pipewire.org/page_tutorial7.html)
- [Microsoft WASAPI loopback recording](https://learn.microsoft.com/windows/win32/coreaudio/loopback-recording)

## Known limitations

- WASAPI loopback capture works only in shared mode;
- Loopback capture can fail while another application holds the source endpoint in exclusive mode;
- Windows internal processing is currently stereo; multichannel sources are downmixed;
- Latency and stability must be validated on the intended devices and drivers;
- Linux requires a running PipeWire service and session manager;
- The Linux source sink must expose FL/FR monitor ports, and the target sink must expose FL/FR input ports;
- Linux tray support depends on StatusNotifier/AppIndicator integration provided by the desktop;
- The UI does not currently display measured latency, queue level, or drift-correction statistics.

## Project layout

```text
src/
  main.cpp                       Application entry point and CLI helper modes
  mainwindow.{h,cpp}             Main window, tray, settings, and reconnect logic
  appicon.{h,cpp}                Application icon
  core/
    audiorouter.{h,cpp}          Backend abstraction and factory
    audiorouter_win.{h,cpp}      Windows WASAPI backend
    audiorouter_linux.{h,cpp}    Linux PipeWire backend
    ringbuffer.h                 Bounded strict-SPSC audio queue
    adaptive_audio_buffer.h      Windows queue-level, drift, and volume control
    realtime_audio.h             Linux real-time stereo processing
tests/
  ringbuffer_test.cpp
  adaptive_audio_buffer_test.cpp
  realtime_audio_test.cpp
  mainwindow_test.cpp
translations/
  audiomonitor_en.ts
  audiomonitor_zh_CN.ts
packaging/
  Linux packaging and installation scripts
```

## Releases and continuous integration

GitHub Actions:

- build the project on Windows and Ubuntu;
- run the automated test suite;
- compile the Qt translation catalogs;
- produce Windows ZIP and MSI packages for version tags;
- produce Linux DEB, RPM, AppImage, and generic archives;
- generate SHA-256 checksums for release assets.

See [`.github/PACKAGING.md`](.github/PACKAGING.md) for packaging details.

## License

This project is available under the [MIT License](LICENSE).
