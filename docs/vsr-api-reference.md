# VSR Pipeline API Reference

Public interfaces and types for HLPlayer's Video Super-Resolution subsystem. All types live in the `hlplayer` namespace. Header paths are relative to `src/core/include/hlplayer/` unless noted otherwise.


## Core Types

### Result\<T\>

**Header:** `Result.h`

Error-aware return type. Every fallible API returns this instead of throwing.

```cpp
template<typename T>
class Result {
    static constexpr Result success(T val);
    static constexpr Result error(PlayerError err);

    constexpr bool hasValue() const noexcept;
    constexpr bool hasError() const noexcept;
    constexpr const T& value() const & noexcept;
    constexpr T&& value() && noexcept;
    constexpr PlayerError error() const noexcept;
    constexpr T value_or(T defaultVal) const;
};
```

The `Result<void>` specialization omits `value()` and `value_or()`. Construct it with `Result<void>::success()` or `Result<void>::error(PlayerError)`.

### PlayerError

**Header:** `Result.h`

```cpp
enum class PlayerError : int32_t {
    None = 0,
    InvalidURL,
    NetworkError,
    DecodeError,
    DeviceLost,
    InvalidState,
    UnsupportedFormat,
    Timeout,
    NeedMoreData,
    Unknown = 999
};
```

### GpuFrame

**Header:** `GpuFrameContract.h`

Carries a single video frame. The pixel data lives on the GPU; `cpuData` is only populated in fallback paths.

```cpp
struct GpuFrame {
    GpuFrameHandle handle;      // native GPU texture handle
    PixelFormat format;
    uint32_t width;
    uint32_t height;
    ColorSpace colorSpace;
    ColorRange colorRange;
    double timestamp;           // presentation timestamp in seconds
    bool deviceLost;
    int seekSerial;             // seek generation counter
    std::vector<uint8_t> cpuData; // optional CPU-side pixels (empty = GPU-only)
};
```

### PixelFormat

```cpp
enum class PixelFormat : uint8_t {
    Unknown = 0,
    NV12,    // 2-plane YUV, 8-bit
    P010,    // 2-plane YUV, 10-bit
    RGBA8,   // 4-channel, 8-bit
    RGBA16F, // 4-channel, half-float
    Vulkan   // opaque Vulkan image
};
```

### EncodedPacket

**Header:** `IVideoEncoder.h`

```cpp
struct EncodedPacket {
    std::vector<uint8_t> data;
    double pts = 0.0;
    double dts = 0.0;
    double duration = 0.0;
    bool isKeyFrame = false;
    uint32_t streamIndex = 0;
};
```


## IPipelineNode

**Header:** `IPipelineNode.h`

Base interface for processing nodes in the pipeline. Decoders, VSR processors, and any other transformation unit implement this.

```cpp
class IPipelineNode {
public:
    virtual ~IPipelineNode() = default;

    virtual Result<void> initialize() = 0;
    virtual Result<GpuFrame> process(const GpuFrame& frame) = 0;
    virtual Result<std::vector<GpuFrame>> flush() = 0;
    virtual void reset() = 0;
    virtual std::string nodeName() const = 0;
    virtual bool isHealthy() const = 0;
};
```

| Method | Description |
|---|---|
| `initialize()` | Set up internal state. Call before any `process()` calls. |
| `process(frame)` | Transform one frame. Return the output frame or an error. |
| `flush()` | Return any frames buffered internally (e.g. temporal models). |
| `reset()` | Clear all buffers and return to initial state. |
| `nodeName()` | Human-readable name for logging. |
| `isHealthy()` | Returns false if the node is in a degraded or failed state. |


## IVideoEncoder

**Header:** `IVideoEncoder.h`

Encodes GPU frames into compressed video packets. Supports H.264, HEVC, and AV1 via NVENC, QSV, or AMF.

### Enums and Structs

```cpp
enum class HwAccelMode : uint8_t {
    None = 0,
    Auto,
    D3D11,
    CUDA,
    Vulkan
};

struct EncoderConfig {
    Codec codec = Codec::H264;
    uint32_t width = 0;
    uint32_t height = 0;
    double frameRate = 30.0;
    uint32_t bitrate = 0;
    uint32_t crf = 23;
    std::string preset = "medium";
    HwAccelMode hwAccel = HwAccelMode::Auto;
    PixelFormat inputFormat = PixelFormat::NV12;
};
```

### Interface

```cpp
class IVideoEncoder {
public:
    virtual ~IVideoEncoder() = default;

    virtual Result<void> open(const EncoderConfig& config) = 0;
    virtual Result<EncodedPacket> encode(const GpuFrame& frame) = 0;
    virtual Result<std::vector<EncodedPacket>> flush() = 0;
    virtual void close() = 0;
    virtual EncoderConfig getConfig() const = 0;
    virtual bool isOpen() const = 0;
};
```

| Method | Description |
|---|---|
| `open(config)` | Configure and open the encoder. Sets codec, resolution, bitrate, hardware backend. |
| `encode(frame)` | Encode one GPU frame. May buffer internally; call `flush()` to drain. |
| `flush()` | Drain all buffered frames. Returns remaining packets. |
| `close()` | Release encoder resources. |
| `getConfig()` | Get the active configuration. |
| `isOpen()` | Check if the encoder is ready for frames. |


## IMuxer

**Header:** `IMuxer.h`

Multiplexes encoded packets into a container file (MP4 or MKV).

### Structs

```cpp
struct MuxerConfig {
    std::string outputPath;
    std::string format = "mp4";    // "mp4" or "mkv"
    std::string mimeType;
    bool fastStart = true;         // move moov atom to front (MP4 only)
};
```

### Interface

```cpp
class IMuxer {
public:
    virtual ~IMuxer() = default;

    virtual Result<void> open(const MuxerConfig& config) = 0;
    virtual Result<uint32_t> addStream(const EncoderConfig& streamConfig) = 0;
    virtual Result<void> writePacket(const EncodedPacket& packet) = 0;
    virtual Result<void> finalize() = 0;
    virtual void close() = 0;
    virtual MuxerConfig getConfig() const = 0;
    virtual uint32_t getStreamCount() const = 0;
    virtual bool isOpen() const = 0;
};
```

| Method | Description |
|---|---|
| `open(config)` | Open output file and configure container format. |
| `addStream(streamConfig)` | Add a video or audio stream. Returns the stream index. |
| `writePacket(packet)` | Write one encoded packet. Interleaves audio/video automatically. |
| `finalize()` | Write trailer and close the file. Required for valid output. |
| `close()` | Release resources without finalizing (for error cleanup). |
| `getStreamCount()` | Number of streams added so far. |


## IVRAMBudgetManager

**Header:** `IVRAMBudgetManager.h`

Thread-safe VRAM budget tracker. Shared across pipeline threads via `shared_ptr`. The concrete implementation queries real GPU memory usage via DXGI 1.6 on Windows.

### Enums and Structs

```cpp
enum class PerformanceMode : uint32_t {
    Performance = 0,   // 90% of available VRAM
    Balanced = 1       // 60% of available VRAM
};

struct VRAMBudgetConfig {
    uint64_t totalBudgetBytes = 0;
    PerformanceMode performanceMode = PerformanceMode::Balanced;
    double warningThreshold = 0.7;
    double degradeThreshold = 0.85;
    double emergencyThreshold = 0.95;
};
```

### Interface

```cpp
class IVRAMBudgetManager {
public:
    virtual ~IVRAMBudgetManager() = default;

    virtual Result<void> initialize(const VRAMBudgetConfig& config) = 0;
    virtual Result<void> requestAllocation(uint64_t sizeBytes, uint32_t timeoutMs = 0) = 0;
    virtual void release(uint64_t sizeBytes) = 0;
    virtual uint64_t usedBytes() const = 0;
    virtual uint64_t availableBytes() const = 0;
    virtual bool isNearLimit() const = 0;
    virtual void setPerformanceMode(PerformanceMode mode) = 0;
    virtual PerformanceMode getPerformanceMode() const = 0;
    virtual void reset() = 0;
};
```

| Method | Description |
|---|---|
| `initialize(config)` | Set budget and thresholds. Call once before any allocations. |
| `requestAllocation(size, timeout)` | Reserve VRAM. Blocks until available or timeout. Returns error on timeout. |
| `release(size)` | Return reserved VRAM. Notifies waiting threads. |
| `usedBytes()` | Currently allocated bytes. |
| `availableBytes()` | Budget minus used. |
| `isNearLimit()` | True when usage exceeds the warning threshold (70% by default). |
| `setPerformanceMode(mode)` | Switch between Performance (90%) and Balanced (60%) budget. Recalculates immediately. |
| `reset()` | Clear all allocations and reset usage to zero. |


## ICheckpointManager

**Header:** `ICheckpointManager.h`

Manages save/restore of processing state for the offline pipeline. All methods are thread-safe.

### Structs

```cpp
struct CheckpointInfo {
    std::string sourcePath;
    std::string outputPath;
    uint64_t lastProcessedFrame;
    uint64_t totalFrames;
    uint64_t timestamp;          // milliseconds since epoch
    std::string pipelineConfig;  // JSON string
};
```

### Interface

```cpp
class ICheckpointManager {
public:
    virtual ~ICheckpointManager() = default;

    virtual Result<void> saveCheckpoint(const CheckpointInfo& info) = 0;
    virtual Result<CheckpointInfo> restoreCheckpoint(const std::string& sourcePath) = 0;
    virtual Result<void> cleanCheckpoint(const std::string& sourcePath) = 0;
    virtual Result<bool> hasCheckpoint(const std::string& sourcePath) = 0;
    virtual Result<CheckpointInfo> getCheckpointInfo(const std::string& sourcePath) = 0;
};
```

| Method | Description |
|---|---|
| `saveCheckpoint(info)` | Write checkpoint to disk. Overwrites if one exists for this source. |
| `restoreCheckpoint(sourcePath)` | Read checkpoint for the given source file. |
| `cleanCheckpoint(sourcePath)` | Delete checkpoint file for this source. |
| `hasCheckpoint(sourcePath)` | Check if a checkpoint exists. |
| `getCheckpointInfo(sourcePath)` | Read checkpoint metadata without restoring. |

Checkpoint files are JSON, stored in the system temp directory under `hlplayer_checkpoints/`. Filename format: `{sourceHash}.checkpoint.json`.


## IModelManager

**Header:** `IModelManager.h`

Discovers and loads super-resolution models from disk. Default directory: `%APPDATA%/HLPlayer/models/`.

### Enums and Structs

```cpp
enum class ModelFormat {
    NCNN,   // .param + .bin pair (Vulkan GPU)
    ONNX    // .onnx single file
};

struct ModelInfo {
    std::string name;
    std::string path;
    float scaleFactor;
    ModelFormat format;
    uint64_t vramEstimateBytes;
    uint32_t inputWidth;
    uint32_t inputHeight;
    std::string description;
};
```

### Interface

```cpp
class IModelManager {
public:
    virtual ~IModelManager() = default;

    virtual Result<void> scanDirectory(const std::string& dirPath) = 0;
    virtual std::vector<ModelInfo> getAvailableModels() const = 0;
    virtual Result<void> loadModel(const std::string& modelId) = 0;
    virtual Result<void> unloadModel(const std::string& modelId) = 0;
    virtual Result<ModelInfo> getModelInfo(const std::string& modelId) const = 0;
    virtual bool isModelLoaded(const std::string& modelId) const = 0;
};
```

| Method | Description |
|---|---|
| `scanDirectory(dirPath)` | Walk the directory tree and register all found models. |
| `getAvailableModels()` | Return metadata for all discovered models (loaded or not). |
| `loadModel(modelId)` | Load model weights into memory. Model ID is filename without extension. |
| `unloadModel(modelId)` | Release model weights from memory. |
| `getModelInfo(modelId)` | Get metadata for a specific model. |
| `isModelLoaded(modelId)` | Check if a model's weights are currently in memory. |


## IHWDecoder

**Header:** `HWDecoder.h`

Hardware video decoder. Produces `GpuFrame` structs with GPU texture handles.

### Enums and Structs

```cpp
enum class DecodeBackend : uint8_t {
    Auto = 0,
    Vulkan,
    CUDA,
    D3D11,
    CPU
};

enum class Codec : uint8_t {
    Unknown = 0,
    H264,
    HEVC,
    AV1
};

struct DecoderConfig {
    DecodeBackend backend = DecodeBackend::Auto;
    Codec codec = Codec::Unknown;
    void* gpuDevice = nullptr;    // VkPhysicalDevice or ID3D11Device
    uint32_t width = 0;
    uint32_t height = 0;
    PixelFormat outputPixelFormat = PixelFormat::NV12;
    std::vector<uint8_t> extradata;
};
```

### Interface

```cpp
class IHWDecoder {
public:
    virtual ~IHWDecoder() = default;

    virtual Result<void> open(const DecoderConfig& config) = 0;
    virtual Result<GpuFrame> decode(const uint8_t* data, size_t size, double pts) = 0;
    virtual Result<std::vector<GpuFrame>> flush() = 0;
    virtual void close() = 0;

    virtual DecodeBackend getBackend() const = 0;
    virtual bool supportsCodec(Codec codec) const = 0;
};
```


## RealtimeVSRPipeline

**Header:** `RealtimeVSRPipeline.h`

Three-thread real-time pipeline: Decode, VSR, Render. The demuxer is external and feeds packets via a callback.

### Configuration

```cpp
struct VSRPipelineConfig {
    size_t packetQueueMaxPackets = 200;      // demux -> decode queue size
    size_t packetQueueMaxBytes = 20 * 1024 * 1024;
    size_t decodedFrameQueueCap = 4;          // decode -> VSR frame queue
    size_t vsrInputQueueCap = 2;              // VSR input (aggressive drop)
    size_t outputFrameQueueCap = 4;           // VSR -> render frame queue
    double vsrSlowThresholdMs = 16.0;         // circuit breaker threshold
    int vsrSlowFrameCount = 3;                // consecutive slow frames to open circuit
    double vsrCooldownSeconds = 30.0;         // cooldown before probing
    uint32_t vramAllocationTimeoutMs = 50;    // VRAM wait timeout per frame
};
```

### Setup Methods

Call these before `initialize()`:

```cpp
void setDecoder(std::shared_ptr<IHWDecoder> decoder);
void setVSRNode(std::shared_ptr<IPipelineNode> vsrNode);  // nullptr = bypass VSR
void setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager);
void setRenderBridge(std::shared_ptr<render::VSRRenderBridge> renderBridge);
void setSyncClock(SyncClock* syncClock);
void setEventBus(EventBus* eventBus);
void setPacketProvider(PacketProvider provider);
void setVSRFrameVRAM(uint64_t bytes);  // per-frame VRAM estimate (default 16MB)
```

`PacketProvider` is a callback the pipeline calls when it needs the next packet:

```cpp
using PacketProvider = std::function<bool(PacketQueue& queue)>;
```

Return `true` if a packet was pushed, `false` on end-of-stream or error.

### Lifecycle

```cpp
Result<void> initialize();   // set up all stages
Result<void> start();        // launch threads
void pause();                // block all threads
void resume();               // unblock
void stop();                 // join threads, release resources
void flush();                // clear all queues
```

### Query Methods

```cpp
VSRState vsrState() const;
bool isRunning() const;
bool isPaused() const;
static const char* vsrStateToString(VSRState state);
uint64_t vsrDroppedFrames() const;     // frames dropped by aggressive queue
uint64_t vsrProcessedFrames() const;   // frames that went through VSR
uint64_t vsrBypassedFrames() const;    // frames that skipped VSR (circuit open)
double lastVSRInferenceMs() const;     // last VSR timing
```

### VSRState

```cpp
enum class VSRState : uint8_t {
    Active = 0,       // normal operation
    CircuitOpen,      // VSR bypassed
    Probing           // testing recovery
};
```


## OfflineTranscodePipeline

**Header:** `src/core/OfflineTranscodePipeline.h`

Six-thread offline pipeline: Demux, Decode, VSR, Encode, Mux, AudioPassthrough.

### Configuration

```cpp
struct OfflineTranscodeConfig {
    // I/O
    std::string inputPath;
    std::string outputPath;
    std::string outputFormat = "mp4";    // "mp4" or "mkv"
    bool fastStart = true;

    // Decoder
    DecodeBackend decodeBackend = DecodeBackend::Auto;

    // VSR
    std::string vsrModelPath;
    int vsrScaleFactor = 2;              // 2, 3, or 4

    // Encoder
    EncoderConfig encoderConfig;

    // Audio
    bool audioPassthrough = true;        // copy audio without re-encoding

    // Checkpoint
    bool enableCheckpoint = true;
    uint64_t checkpointInterval = 100;   // save every N frames

    // Queue sizing
    size_t packetQueueSize = 200;
    size_t frameQueueSize = 8;
    size_t encodedQueueSize = 200;
};
```

### Progress Reporting

```cpp
struct TranscodeProgress {
    uint64_t framesProcessed = 0;
    uint64_t totalFrames = 0;
    double currentFps = 0.0;
    double estimatedSecondsLeft = 0;
    std::string stage;
    bool isComplete = false;
};
```

### Setup Methods

```cpp
void setDecoder(std::shared_ptr<IHWDecoder> decoder);
void setEncoder(std::shared_ptr<IVideoEncoder> encoder);
void setMuxer(std::shared_ptr<IMuxer> muxer);
void setCheckpointManager(std::shared_ptr<ICheckpointManager> checkpointManager);
void setVRAMBudgetManager(std::shared_ptr<IVRAMBudgetManager> vramManager);
```

### Lifecycle

```cpp
Result<void> configure(const OfflineTranscodeConfig& config);
Result<void> start();
void cancel();
void pause();
void resume();
Result<void> waitUntilComplete();
TranscodeProgress getProgress() const;
```

### Callbacks

```cpp
using ProgressCallback = std::function<void(const TranscodeProgress&)>;
void setProgressCallback(ProgressCallback callback);

using ErrorCallback = std::function<void(PlayerError, const std::string&)>;
void setErrorCallback(ErrorCallback callback);
```

### State Machine

```cpp
enum class State : uint8_t {
    Idle = 0,
    Configured,
    Running,
    Paused,
    Completed,
    Cancelled,
    Error
};

State getState() const;
std::string stateToString(State state) const;
```


## VSRCircuitBreaker

**Header:** `src/core/VSRCircuitBreaker.h`

Standalone circuit breaker state machine for VSR inference. Used by both pipelines.

### States

```cpp
enum class VSRBreakerState : uint8_t {
    Active = 0,       // VSR running normally
    CircuitOpen,      // VSR bypassed
    Probing,          // testing one frame for recovery
    Disabled          // manual override off
};
```

### VRAM Degradation Actions

```cpp
enum class VRAMDegradationAction : uint8_t {
    None = 0,         // VRAM fine
    ReduceScale,      // drop scale factor (e.g. 4x -> 2x)
    DisableVSR        // emergency: force circuit open
};
```

### Configuration

```cpp
struct VSRCircuitBreakerConfig {
    double slowThresholdMs = 16.0;
    int slowFrameCount = 3;
    double cooldownSeconds = 30.0;
    double vramDegradationThreshold = 0.85;
    double vramEmergencyThreshold = 0.95;
    double degradedScaleFactor = 2.0;
};
```

### API

```cpp
class VSRCircuitBreaker {
public:
    explicit VSRCircuitBreaker(VSRCircuitBreakerConfig config = {});

    // State queries
    VSRBreakerState state() const;
    bool shouldBypass() const;              // true if CircuitOpen or Disabled
    int consecutiveSlowFrames() const;
    static const char* stateToString(VSRBreakerState state);

    // State transitions
    void recordInference(double durationMs); // feed timing after each VSR call
    void forceDisable();                     // any state -> Disabled
    void forceEnable();                      // Disabled -> Active
    void reset();                            // -> Active

    // Probing (called by pipeline integration)
    bool tryStartProbing();                  // returns true if transitioned to Probing
    void handleProbeResult(double inferenceMs);

    // VRAM pressure
    double recommendedScaleFactor() const;   // 0.0 = use default
    VRAMDegradationAction checkVRAMPressure(IVRAMBudgetManager* vramManager);

    // Configuration
    void setEventBus(EventBus* eventBus);
    void updateConfig(VSRCircuitBreakerConfig config);
    VSRCircuitBreakerConfig config() const;
};
```

### State Transitions

| From | To | Trigger |
|---|---|---|
| Active | CircuitOpen | `slowFrameCount` consecutive frames above `slowThresholdMs` |
| CircuitOpen | Probing | `cooldownSeconds` elapsed, pipeline calls `tryStartProbing()` |
| Probing | Active | Probe frame under threshold |
| Probing | CircuitOpen | Probe frame still over threshold |
| Any | Disabled | `forceDisable()` |
| Disabled | Active | `forceEnable()` |


## VSRSyncCompensator

**Header:** `VSRSyncCompensator.h`

Adjusts frame PTS values to account for VSR processing latency. Maintains A/V sync in the real-time pipeline.

### Constants

```cpp
static constexpr double kDropThresholdSec = 0.1;     // drop frames >100ms late
static constexpr size_t kLatencyWindowSize = 30;      // rolling average window
```

### Configuration

```cpp
void setSyncClock(SyncClock* syncClock);
void setEventBus(EventBus* eventBus);
void setDropThreshold(double thresholdSec);
```

### Latency Tracking

```cpp
void updateInferenceLatency(double durationMs);   // feed after each VSR frame
double getAverageLatency() const;                  // average in ms
double getAverageLatencySec() const;               // average in seconds
```

### PTS Compensation

```cpp
double compensatePTS(GpuFrame& frame);    // adjust frame.timestamp, update SyncClock
double compensatePTSValue(double pts) const; // adjust a raw PTS value
```

`compensatePTS` subtracts the rolling average latency from the frame's timestamp and pushes the adjusted value to `SyncClock::videoClock`.

### Frame Drop Logic

```cpp
bool shouldDropFrame(const GpuFrame& frame);
bool shouldDropFrameAggressive(const GpuFrame& frame, size_t queueDepth, size_t queueCapacity);
```

- `shouldDropFrame`: returns true if the frame is more than `dropThreshold` seconds behind the master clock.
- `shouldDropFrameAggressive`: also drops when the queue is at capacity, even if the frame isn't late.

### Stats

```cpp
uint64_t totalDroppedFrames() const;
uint64_t totalCompensatedFrames() const;
uint64_t totalAggressiveDrops() const;
size_t latencySampleCount() const;
void reset();   // clear all state
```


## Configuration Structs Summary

| Struct | Purpose | Key fields |
|---|---|---|
| `EncoderConfig` | Video encoder settings | codec, resolution, bitrate, CRF, preset, hwAccel |
| `MuxerConfig` | Container output settings | outputPath, format, fastStart |
| `VRAMBudgetConfig` | VRAM budget management | totalBudgetBytes, performanceMode, thresholds |
| `VSRPipelineConfig` | Real-time pipeline tuning | queue sizes, circuit breaker params, VRAM timeout |
| `OfflineTranscodeConfig` | Offline pipeline settings | I/O paths, VSR model, encoder config, checkpoint settings |
| `VSRCircuitBreakerConfig` | Circuit breaker tuning | slowThreshold, slowFrameCount, cooldown, VRAM thresholds |
| `DecoderConfig` | Hardware decoder setup | backend, codec, gpuDevice, resolution, pixel format |
| `CheckpointInfo` | Checkpoint data | sourcePath, outputPath, lastProcessedFrame, totalFrames, config |
| `ModelInfo` | Model metadata | name, path, scaleFactor, format, vramEstimateBytes |
| `TranscodeProgress` | Progress reporting | framesProcessed, totalFrames, fps, ETA, stage |
