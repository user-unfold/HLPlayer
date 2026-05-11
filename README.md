# HLPlayer

A cross-platform multimedia player built with **Qt6/QML** and **FFmpeg**, featuring DirectShow camera capture, NVENC hardware encoding, RTMP live streaming, NCNN Vulkan AI super-resolution, and real-time video rendering.

## Features

### Video Playback
- Hardware-accelerated decoding via FFmpeg (DXVA2 / D3D11VA / CUDA)
- Custom QML video output with D3D11 rendering pipeline
- Audio/video synchronization with master clock strategy
- Playlist support with loop / sequential / shuffle modes
- Playback speed control

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

### Architecture
- Modular CMake build system with clean library separation
- Interface-based design (`IVideoEncoder`, `IAudioDecoder`, `IAudioRenderer`, etc.)
- Lock-free concurrent queues for inter-thread communication
- Event bus for decoupled component communication
- State machine driven player lifecycle

## Project Structure

```
HLPlayer/
├── src/
│   ├── app/            # Application entry point
│   ├── core/           # Player engine, state machine, sync, AI pipeline
│   ├── extractor/      # FFmpeg demuxer, decoders, muxer, HW device pool
│   ├── camera/         # DirectShow capture, NVENC encoding, recording
│   ├── render/         # D3D11 video rendering, SDL/Qt audio output
│   ├── vsr/            # NCNN super-resolution module
│   └── qml/            # QML UI components and bridges
├── resources/          # Icons, QML resources
├── ffmpeg-7.1.1/       # FFmpeg dev headers (build dependency)
└── CMakeLists.txt
```

## Dependencies

| Dependency | Version | Purpose |
|---|---|---|
| Qt6 (Core, Gui, Qml, Quick) | 6.x | UI framework |
| FFmpeg | 7.1.1 | Demuxing, decoding, encoding, muxing |
| NCNN | - | Neural network inference |
| Vulkan | - | GPU compute for AI models |
| SDL2 | 2.30.8 | Audio output (alternative) |
| spdlog | 1.17.0 | Logging (fetched by CMake) |
| nlohmann/json | 3.11.3 | JSON parsing (fetched by CMake) |

## Build

```bash
# Configure
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release
```

### Prerequisites
- CMake 3.22+
- C++20 compiler (MSVC / GCC / Clang)
- Qt6 development libraries
- FFmpeg 7.1.1 development headers and libraries
- Vulkan SDK (for VSR feature)
- NCNN (for VSR feature)

## Tech Stack

- **Language**: C++20
- **UI**: Qt6 QML
- **Media**: FFmpeg 7.1.1
- **AI Inference**: NCNN + Vulkan
- **GPU Encoding**: NVIDIA NVENC
- **Audio**: SDL2 / Qt Multimedia
- **Build**: CMake

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

Third-party libraries are subject to their respective licenses:
- FFmpeg — LGPL 2.1 / GPL 2.0
- NCNN — BSD 3-Clause
- Qt6 — LGPL 3.0
- SDL2 — zlib License
