# HLPlayer

A cross-platform multimedia player built with **Qt6/QML** and **FFmpeg**, featuring real-time ASR subtitles with SRT export, NCNN Vulkan AI super-resolution, DirectShow camera capture, NVENC hardware encoding, and RTMP live streaming.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue) ![Qt6](https://img.shields.io/badge/Qt-6.5-green) ![FFmpeg](https://img.shields.io/badge/FFmpeg-7.1.1-red) ![License](https://img.shields.io/badge/License-MIT-yellow)

## Features

### Video Playback
- Hardware-accelerated decoding via FFmpeg (D3D11VA / DXVA2 / CUDA)
- Custom QML video output with D3D11 rendering pipeline
- Wall-clock-based A/V sync for jitter-free playback
- Playlist support with loop / sequential / shuffle modes
- Playback speed control (0.25x – 4.0x)

### Real-time ASR Subtitles
- GPU-accelerated speech recognition powered by [whisper.cpp](https://github.com/ggerganov/whisper.cpp) + Vulkan
- VAD (Voice Activity Detection) with Silero v6.2 for accurate segmentation
- Sentence splitting at CJK punctuation boundaries (。！？)
- Text deduplication across segments (codepoint-level suffix matching)
- Glass-style subtitle overlay with fade-in/out animations
- **SRT export** — auto-save subtitles on playback end with custom filename and directory

### AI Super-Resolution (VSR)
- Real-time video super-resolution powered by [NCNN](https://github.com/Tencent/ncnn) + Vulkan GPU
- Multiple model support: RealESR-animevideov3 (x2/x3/x4), RealESRGAN-x4plus
- Offline transcoding pipeline for batch processing
- VRAM budget management and circuit breaker for stability

### Camera & Recording
- DirectShow camera capture with real-time preview
- NVENC H.264 hardware encoding with GPU fallback chain
- AAC audio encoding
- Pause/resume recording, MP4 output
- RTMP live streaming to custom servers

## Project Structure

```
HLPlayer/
├── src/
│   ├── app/            # Application entry point
│   ├── core/           # Player engine, state machine, sync clock, event bus
│   ├── extractor/      # FFmpeg demuxer, decoders, muxer, HW device pool
│   ├── asr/            # Whisper ASR pipeline, subtitle manager, QML bridge
│   ├── camera/         # DirectShow capture, NVENC encoding, recording
│   ├── render/         # D3D11 video rendering, SDL audio output
│   ├── vsr/            # NCNN super-resolution module
│   └── qml/            # QML UI components and C++ bridges
├── 3rdparty/
│   └── whisper.cpp/    # whisper.cpp (submodule)
├── ffmpeg-7.1.1/       # FFmpeg dev headers + link libraries
├── models/             # Whisper & NCNN model files (download separately)
└── CMakeLists.txt
```

## Quick Start (Windows)

### Prerequisites

| Dependency | Version | Notes |
|---|---|---|
| CMake | 3.22+ | |
| MinGW-w64 / MSVC | C++20 | MinGW recommended |
| Qt6 | 6.5.3 | Core, Gui, Qml, Quick |
| FFmpeg | 7.1.1 | Dev headers + libs included in repo |
| SDL2 | 2.30.8 | Audio output |
| Vulkan SDK | Latest | GPU compute for whisper.cpp / NCNN |

### Build

```bash
# Configure (MinGW)
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release -j%NUMBER_OF_PROCESSORS%
```

### Download Models

Place Whisper models in the `models/` directory:

```bash
# ASR model (required for subtitles)
# Download from https://huggingface.co/ggerganov/whisper.cpp/tree/main
# Recommended: ggml-large-v3-turbo.bin (fast + accurate)

# VAD model (required for speech segmentation)
# ggml-silero-v6.2.0.bin

# Super-resolution models (optional)
# realesr-animevideov3-x2.bin + .param
```

### Run

The built executable is at `build/src/app/hlplayer_app.exe`. All required DLLs (Qt6, FFmpeg, SDL2) are deployed to the same directory by the build system.

## Dependencies

| Dependency | Version | Purpose | Fetched by CMake |
|---|---|---|---|
| Qt6 (Core, Gui, Qml, Quick) | 6.5.3 | UI framework | No |
| FFmpeg | 7.1.1 | Demuxing, decoding, encoding | No (headers in repo) |
| whisper.cpp | latest | ASR inference | Submodule |
| NCNN | - | Neural network inference | No |
| SDL2 | 2.30.8 | Audio output | No |
| spdlog | 1.17.0 | Logging | Yes |
| nlohmann/json | 3.11.3 | JSON parsing | Yes |

## Architecture

- **Modular CMake** — clean library separation (core / extractor / asr / render / vsr / qml)
- **Interface-based design** — `IDemuxer`, `IHWDecoder`, `IAudioDecoder`, `IAudioRenderer`, `IVideoFrameSink`
- **Lock-free queues** — bounded `PacketQueue` and `VideoFrameQueue` with condition variables
- **Event bus** — decoupled component communication via `EventBus::publish/subscribe`
- **State machine** — `PlayerState` (Idle → Buffering → Playing → Paused → End) with transition validation
- **Sync clock** — `ExternalClock` mode for wall-clock-driven A/V sync; `AudioMaster` as fallback
- **ASR pipeline** — audio resampling → VAD → whisper inference → text dedup → QML bridge → subtitle overlay

## Tech Stack

- **Language**: C++20
- **UI**: Qt6 QML
- **Media**: FFmpeg 7.1.1
- **ASR**: whisper.cpp + Vulkan GPU
- **AI Inference**: NCNN + Vulkan
- **GPU Encoding**: NVIDIA NVENC
- **Audio**: SDL2
- **Build**: CMake + Ninja / MinGW Make

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

Third-party libraries are subject to their respective licenses:
- FFmpeg — LGPL 2.1 / GPL 2.0
- whisper.cpp — MIT
- NCNN — BSD 3-Clause
- Qt6 — LGPL 3.0
- SDL2 — zlib License
