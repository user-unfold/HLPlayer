# VSR Pipeline Architecture

This document covers the architecture of HLPlayer's Video Super-Resolution (VSR) subsystem. Two pipelines are provided: one for real-time playback and one for offline file transcoding.

## Module Overview

```
                         ┌──────────────────────┐
                         │     IModelManager     │
                         │  Scan/load SR models  │
                         └──────────┬───────────┘
                                    │
                    ┌───────────────┼───────────────────┐
                    ▼               ▼                   ▼
         ┌─────────────────┐ ┌───────────────┐ ┌────────────────────┐
         │  RealtimeVSR    │ │  Offline       │ │  VSRCircuitBreaker │
         │  Pipeline       │ │  Transcode     │ │  (fault tolerance) │
         │  (3 threads)    │ │  Pipeline      │ └────────────────────┘
         └────────┬────────┘ │  (6 threads)   │
                  │          └───────┬────────┘
                  ▼                  ▼
         ┌─────────────────┐ ┌───────────────┐
         │ VSRRenderBridge │ │ FFmpegMuxer   │
         │ (D3D11/Vulkan)  │ │ (MP4/MKV)    │
         └─────────────────┘ └───────────────┘
                    │                  │
                    ▼                  ▼
         ┌─────────────────┐ ┌───────────────┐
         │ IVRAMBudget     │ │ ICheckpoint   │
         │ Manager         │ │ Manager       │
         └─────────────────┘ └───────────────┘
```

The shared interfaces sit in `src/core/include/hlplayer/`. Concrete implementations live under `src/core/` and `src/vsr/`.


## Real-Time VSR Pipeline

The real-time pipeline processes live or playing video. It keeps latency low by running each stage in its own thread, using aggressive frame dropping when the VSR stage can't keep up.

### Data Flow

```
External        ┌──────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐
Demuxer  ───►  │  Packet   │──► │  Decode   │──► │  VSR Input   │──► │   VSR    │
(PacketProvider)│  Queue    │    │  Thread   │    │  Queue (cap2)│    │  Thread  │
               └──────────┘    └──────────┘    └──────────────┘    └────┬─────┘
                                                                        │
          Active: super-resolve ─────────────────────────────────────────┤
          CircuitOpen: pass-through (no processing)                      │
          Probing: test one frame to check recovery                      │
                                                                        ▼
                                                                 ┌──────────────┐
                                                                 │   Output     │
                                                                 │  Queue (cap4)│
                                                                 └──────┬───────┘
                                                                        │
                                                                        ▼
                                                                 ┌──────────────┐
                                                                 │   Render     │
                                                                 │   Thread     │
                                                                 └──────┬───────┘
                                                                        │
                                                                        ▼
                                                                 VSRRenderBridge
                                                                 (D3D11/Vulkan)
```

### Thread Model

Three threads run inside `RealtimeVSRPipeline`:

| Thread | Job | Queue connections |
|---|---|---|
| **Decode** | Pull packets from `PacketQueue`, decode via `IHWDecoder`, push `GpuFrame` into `VSRInputQueue` | Reads `PacketQueue`, writes `VSRInputQueue` |
| **VSR** | Pull frames from `VSRInputQueue`, run super-resolution via `IPipelineNode`, push to `OutputQueue` | Reads `VSRInputQueue`, writes `OutputQueue` |
| **Render** | Pull frames from `OutputQueue`, present via `VSRRenderBridge` | Reads `OutputQueue` |

The demuxer is external. The pipeline uses a `PacketProvider` callback to request the next packet from whoever owns the demux. This keeps the pipeline decoupled from any specific container format.

All three threads share a single `pauseMutex_` / `pauseCv_` pair for pause/resume. When paused, each thread blocks on the condition variable between items.

### VSR Input Queue: Aggressive Dropping

The queue between Decode and VSR has a capacity of 2 frames. When it's full and a new frame arrives, the oldest frame is discarded. This keeps the VSR stage always processing the most recent content, trading completeness for latency.

The dropped count is tracked and queryable via `vsrDroppedFrames()`.

### Sync Compensation

`VSRSyncCompensator` sits between the VSR output and the renderer. It does two things:

1. **PTS adjustment.** After each VSR inference, the compensator records the duration. It maintains a rolling average over the last 30 samples. When a frame exits VSR, its PTS is shifted backward by the average latency so downstream sync logic sees the frame at the right wall-clock time.

2. **Late frame dropping.** If a frame's PTS is more than 100ms behind the master clock, it gets dropped. An aggressive variant also drops frames when the output queue is at capacity.

Stats (dropped, compensated, aggressive drops) are tracked as atomics and queryable at any time.


## Offline Transcode Pipeline

The offline pipeline reads a source file, applies VSR to every frame, and writes the result to MP4 or MKV. It trades latency for quality: every frame gets processed, there's no frame dropping, and checkpoints let you resume interrupted jobs.

### Data Flow

```
┌──────────┐    ┌──────────┐    ┌──────────────┐    ┌──────────┐    ┌──────────────┐
│  Demux   │──► │  Decode   │──► │     VSR      │──► │  Encode  │──► │    Mux       │
│  Thread  │    │  Thread   │    │   Thread     │    │  Thread  │    │   Thread     │
└──────────┘    └──────────┘    └──────────────┘    └──────────┘    └──────┬───────┘
                                                                         │
                                                                         ▼
                                                                  Output File
                                                                  (MP4/MKV)

               ┌──────────────────────────────┐
               │     Audio Passthrough         │
               │     Thread (copy, no decode)  │──────────────────────► Mux
               └──────────────────────────────┘
```

### Thread Model

Six threads run inside `OfflineTranscodePipeline`:

| Thread | Job |
|---|---|
| **Demux** | Read input file via FFmpeg, push video packets to `videoPacketQueue_`, audio packets to `audioPacketQueue_` |
| **Decode** | Pull video packets, decode via `IHWDecoder`, push `GpuFrame` to `decodedFrameQueue_` |
| **VSR** | Pull decoded frames, super-resolve via `IPipelineNode`, push to `vsrFrameQueue_` |
| **Encode** | Pull super-resolved frames, encode via `IVideoEncoder`, push `EncodedPacket` to `encodedQueue_` |
| **Mux** | Pull encoded packets, write via `IMuxer` |
| **Audio Passthrough** | Pull audio packets from `audioPacketQueue_`, wrap as `EncodedPacket`, push to muxer |

The audio passthrough thread copies audio without re-encoding. It wraps raw audio packets into `EncodedPacket` structs and feeds them directly to the muxer alongside the encoded video.

### Checkpoint and Resume

Every 100 frames, the pipeline saves a `CheckpointInfo` via `ICheckpointManager`. The checkpoint records:

- Source and output file paths
- Last processed frame number
- Total frame count
- Timestamp
- Pipeline configuration as JSON

On restart, if a checkpoint exists and a `.part` temp file is present, processing resumes from `lastProcessedFrame + 1`. The `ResumeManager` class handles the lifecycle: temp file creation, checkpoint validation, finalization (rename `.part` to final output), and cleanup.

Checkpoint files are stored as JSON in the system temp directory under `hlplayer_checkpoints/`, named `{sourceHash}.checkpoint.json`.


## Zero-Copy GPU Memory Path

Frames stay on the GPU from decode through to render or encode. No CPU-side pixel copies happen in the hot path.

### How It Works

1. **Decode.** `IHWDecoder` produces `GpuFrame` structs where `handle.nativeHandle` is a GPU texture pointer (ID3D11Texture2D* or VkImage). The `cpuData` vector is empty.

2. **VSR.** For NCNN, the frame's GPU texture is imported via Vulkan external memory (`NCNNInterop`). NCNN operates on the texture directly. For the CPU fallback path in `NcnnSuperResolution`, `cpuData` is used, but this is the degraded mode, not the fast path.

3. **Render (real-time).** `VSRRenderBridge` uses `CopySubresourceRegion` on D3D11 or stores the VkImage for Qt RHI import on Vulkan. The staging texture pool matches the source format (NV12, P010, RGBA8, RGBA16F).

4. **Encode (offline).** `HWVideoEncoder` calls `av_hwframe_get_buffer` and stores the native handle as an opaque reference in `data[3]`. NVENC/AMF/QSV pick up the GPU texture directly.

### Format Chain

```
Decoder output: NV12 or P010 (GPU texture)
        │
        ▼
VSR input: RGBA8 (via format conversion in NCNN or staging texture)
        │
        ▼
VSR output: RGBA8 (GPU texture)
        │
        ├──► Render: staging texture in source format, D3D11/Vulkan present
        │
        └──► Encode: GPU texture passed to NVENC/AMF/QSV via FFmpeg hw frames
```


## Fault Tolerance: Circuit Breaker

The circuit breaker protects the real-time pipeline from sustained slow VSR inference. Without it, a slow model would back up the entire pipeline and stall playback.

### States

```
                 slowThreshold exceeded
                 slowFrameCount times
        ┌─────┐ ◄──────────────────────── ┌─────────────┐
        │Active│                           │ CircuitOpen │
        └──┬───┘                           └──────┬──────┘
           │                                      │
           │ probe frame succeeds                 │ cooldownSeconds elapsed
           │ (inference < threshold)              │
           │                                      ▼
           │ ◄──────────────────────────── ┌─────────┐
           │                               │ Probing │
           └──────────────────────────────►└─────────┘
           probe frame too slow             │
                                           ▼
                                     back to CircuitOpen

        Any state ──forceDisable()──► Disabled
        Disabled ──forceEnable()──►  Active
```

- **Active.** VSR runs on every frame. Inference times are tracked.
- **CircuitOpen.** VSR is bypassed. Frames pass through unchanged. Entered when `slowFrameCount` consecutive frames exceed `slowThresholdMs` (default: 3 frames over 16ms).
- **Probing.** After a cooldown (default 30 seconds), one frame is tested with VSR. If it's fast enough, the breaker returns to Active. If not, it goes back to CircuitOpen and the cooldown resets.
- **Disabled.** Manual override. VSR never runs. Entered via `forceDisable()`, exited via `forceEnable()`.

### VRAM Degradation

The circuit breaker also watches VRAM pressure through `IVRAMBudgetManager`:

| VRAM Usage | Action |
|---|---|
| Below 85% | None (normal operation) |
| 85-95% | `ReduceScale`: recommend lowering scale factor (e.g. 4x to 2x) |
| Above 95% | `DisableVSR`: force circuit open |

### Decode Fallback Chain

When the hardware decoder fails (device lost, unsupported codec), the pipeline can fall back:

1. **D3D11** hardware decode (primary on Windows)
2. **CUDA** hardware decode (if NVIDIA GPU available)
3. **CPU** software decode (last resort)

The `DecodeBackend::Auto` setting tries these in order. Each `IHWDecoder` implementation reports `supportsCodec()` so the pipeline can pick the right one.


## VRAM Budget Management

`IVRAMBudgetManager` tracks GPU memory usage and enforces budget limits. The concrete implementation uses DXGI 1.6 on Windows to query real VRAM usage from the GPU driver.

### Performance Modes

| Mode | VRAM Budget | When to use |
|---|---|---|
| `Performance` | 90% of available VRAM | Max quality, VSR-heavy workflows |
| `Balanced` | 60% of available VRAM | Stable playback, shared with other apps |

### Thresholds

The manager has three thresholds that trigger logging and degradation:

- **Warning** (70%): logged once, no action
- **Degradation** (85%): logged once, circuit breaker starts recommending scale reduction
- **Emergency** (95%): logged once, circuit breaker forces VSR off

### Allocation Flow

Before VSR inference, the pipeline calls `requestAllocation(bytes, timeoutMs)`. If VRAM is available, the allocation succeeds immediately. If not, it blocks up to `timeoutMs` (default 50ms for real-time, 0 for non-blocking). After inference, `release(bytes)` returns the budget.

On non-Windows platforms, or if DXGI queries fail, the manager falls back to a static 4GB budget estimate.


## Model Management

`IModelManager` handles discovery and loading of super-resolution models. The default model directory is `%APPDATA%/HLPlayer/models/`.

### Supported Formats

| Format | Files | Notes |
|---|---|---|
| NCNN | `.param` + `.bin` pair | Vulkan GPU inference, primary format |
| ONNX | `.onnx` single file | CPU/GPU inference via ONNX Runtime |

### Discovery

`scanDirectory()` walks the given path recursively. NCNN models are matched by finding `.param` + `.bin` pairs. ONNX models are matched by `.onnx` extension.

Model metadata is extracted from filenames and file contents:

- **Scale factor:** parsed from filename patterns like `esrgan_4x.param` (regex `_?(\d+)x`), falling back to 2x if not found
- **Input dimensions:** parsed from the `.param` file's `Input` directive
- **VRAM estimate:** file size * scale factor * 1.5 (heuristic)


## Audio Passthrough

The offline pipeline copies audio without re-encoding. The audio passthrough thread reads audio packets from the demuxer, wraps them as `EncodedPacket` structs (preserving PTS, DTS, duration, keyframe flag), and feeds them to the muxer.

This means audio quality is preserved exactly, and no CPU time is spent on audio encoding. The muxer interleaves audio and video packets via `av_interleaved_write_frame`.


## Error Handling

All fallible operations return `Result<T>`. This is a discriminated union that either holds a value (`hasValue() == true`) or an error code (`hasError() == true`).

```cpp
auto result = decoder->decode(data, size, pts);
if (result.hasError()) {
    // result.error() is a PlayerError enum value
    handleFailure(result.error());
} else {
    GpuFrame frame = result.value();
}
```

Error codes defined in `PlayerError`:

| Code | Meaning |
|---|---|
| `None` | No error |
| `InvalidURL` | Bad source path or URL |
| `NetworkError` | Network read failure |
| `DecodeError` | Hardware decode failure |
| `DeviceLost` | GPU device removed |
| `InvalidState` | Operation called in wrong pipeline state |
| `UnsupportedFormat` | Codec or container not supported |
| `Timeout` | Operation timed out (e.g. VRAM allocation) |
| `NeedMoreData` | Decoder needs more input |
| `Unknown` | Catch-all |
