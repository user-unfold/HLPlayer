#include <hlplayer/RealtimeVSRPipeline.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IPipelineNode.h>
#include <hlplayer/IVRAMBudgetManager.h>
#include <hlplayer/PacketQueue.h>
#include <hlplayer/FrameQueue.h>
#include <hlplayer/SyncClock.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/Result.h>
#include "VSRRenderBridge.h"

#include <spdlog/spdlog.h>

#include <chrono>
#include <utility>

namespace hlplayer {

// ============================================================================
// VSRState helpers
// ============================================================================

const char* RealtimeVSRPipeline::vsrStateToString(VSRState state) {
    switch (state) {
        case VSRState::Active:      return "Active";
        case VSRState::CircuitOpen: return "CircuitOpen";
        case VSRState::Probing:     return "Probing";
    }
    return "Unknown";
}

// ============================================================================
// RealtimeVSRPipeline
// ============================================================================

RealtimeVSRPipeline::RealtimeVSRPipeline(VSRPipelineConfig config)
    : config_(std::move(config))
    , packetQueue_(std::make_unique<PacketQueue>(
          config_.packetQueueMaxPackets, config_.packetQueueMaxBytes))
    , vsrInputQueue_(std::make_unique<VSRFrameQueue>(config_.vsrInputQueueCap))
    , outputQueue_(std::make_unique<VideoFrameQueue>(config_.outputFrameQueueCap))
{}

RealtimeVSRPipeline::~RealtimeVSRPipeline() {
    stop();
}

void RealtimeVSRPipeline::setDecoder(std::shared_ptr<IHWDecoder> decoder) {
    decoder_ = std::move(decoder);
}

void RealtimeVSRPipeline::setVSRNode(std::shared_ptr<IPipelineNode> vsrNode) {
    vsrNode_ = std::move(vsrNode);
}

void RealtimeVSRPipeline::setVRAMBudgetManager(
    std::shared_ptr<IVRAMBudgetManager> vramManager) {
    vramManager_ = std::move(vramManager);
}

void RealtimeVSRPipeline::setRenderBridge(
    std::shared_ptr<render::VSRRenderBridge> renderBridge) {
    renderBridge_ = std::move(renderBridge);
}

void RealtimeVSRPipeline::setSyncClock(SyncClock* syncClock) {
    syncClock_ = syncClock;
}

void RealtimeVSRPipeline::setEventBus(EventBus* eventBus) {
    eventBus_ = eventBus;
}

void RealtimeVSRPipeline::setPacketProvider(PacketProvider provider) {
    packetProvider_ = std::move(provider);
}

void RealtimeVSRPipeline::setVSRFrameVRAM(uint64_t bytes) {
    vsrFrameVRAM_ = bytes;
}

// ============================================================================
// Lifecycle
// ============================================================================

Result<void> RealtimeVSRPipeline::initialize() {
    if (initialized_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!decoder_) {
        spdlog::error("[VSRPipeline] No decoder configured");
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (vsrNode_) {
        auto result = vsrNode_->initialize();
        if (result.hasError()) {
            spdlog::warn("[VSRPipeline] VSR node initialization failed ({}), "
                         "running in pass-through mode", static_cast<int>(result.error()));
            vsrNode_.reset();
        }
    }

    initialized_.store(true, std::memory_order_release);
    spdlog::info("[VSRPipeline] Initialized (VSR {})",
                 vsrNode_ ? "enabled" : "disabled");
    return Result<void>::success();
}

Result<void> RealtimeVSRPipeline::start() {
    if (!initialized_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }
    if (running_.load()) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    stopRequested_.store(false, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    running_.store(true, std::memory_order_release);

    packetQueue_->restart();
    vsrInputQueue_->restart();
    outputQueue_->restart();

    vsrState_.store(VSRState::Active, std::memory_order_release);
    consecutiveSlowFrames_ = 0;
    probeRequested_.store(false, std::memory_order_release);

    decodeThread_ = std::thread(&RealtimeVSRPipeline::decodeStage, this);
    vsrThread_ = std::thread(&RealtimeVSRPipeline::vsrStage, this);
    renderThread_ = std::thread(&RealtimeVSRPipeline::renderStage, this);

    spdlog::info("[VSRPipeline] Started (decode+vsr+render threads)");
    return Result<void>::success();
}

void RealtimeVSRPipeline::pause() {
    if (!running_.load()) return;
    paused_.store(true, std::memory_order_release);
    spdlog::info("[VSRPipeline] Paused");
}

void RealtimeVSRPipeline::resume() {
    paused_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(pauseMutex_);
        pauseCv_.notify_all();
    }
    spdlog::info("[VSRPipeline] Resumed");
}

void RealtimeVSRPipeline::stop() {
    if (!running_.exchange(false)) return;

    stopRequested_.store(true, std::memory_order_release);
    paused_.store(false, std::memory_order_release);
    {
        std::lock_guard lock(pauseMutex_);
        pauseCv_.notify_all();
    }

    packetQueue_->shutdown();
    vsrInputQueue_->shutdown();
    outputQueue_->shutdown();

    joinAllThreads();

    if (vsrNode_) {
        vsrNode_->reset();
    }

    vsrState_.store(VSRState::Active, std::memory_order_release);
    consecutiveSlowFrames_ = 0;
    spdlog::info("[VSRPipeline] Stopped");
}

void RealtimeVSRPipeline::flush() {
    packetQueue_->flush();
    vsrInputQueue_->flush();
    outputQueue_->flush();
}

// ============================================================================
// Decode Stage
// ============================================================================

void RealtimeVSRPipeline::decodeStage() {
    spdlog::debug("[VSRPipeline::Decode] Thread started");

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::unique_lock lock(pauseMutex_);
            pauseCv_.wait(lock, [this] {
                return !paused_.load(std::memory_order_acquire) ||
                       stopRequested_.load(std::memory_order_acquire);
            });
            if (stopRequested_.load(std::memory_order_acquire)) break;
        }

        if (!packetProvider_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        bool gotPacket = packetProvider_(*packetQueue_);
        if (!gotPacket) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        auto pkt = packetQueue_->pop(0);
        if (!pkt) continue;

        if (pkt->streamType != StreamType::Video) continue;

        auto frameResult = decoder_->decode(
            pkt->data.data(), pkt->data.size(), pkt->pts);

        if (frameResult.hasError()) {
            spdlog::warn("[VSRPipeline::Decode] Decode error: {}",
                         static_cast<int>(frameResult.error()));
            continue;
        }

        auto frame = std::move(frameResult.value());
        if (frame.width == 0 || frame.height == 0) continue;

        vsrInputQueue_->pushOrDrop(std::move(frame));
    }

    spdlog::debug("[VSRPipeline::Decode] Thread exiting");
}

// ============================================================================
// VSR Stage
// ============================================================================

void RealtimeVSRPipeline::vsrStage() {
    spdlog::debug("[VSRPipeline::VSR] Thread started");

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::unique_lock lock(pauseMutex_);
            pauseCv_.wait(lock, [this] {
                return !paused_.load(std::memory_order_acquire) ||
                       stopRequested_.load(std::memory_order_acquire);
            });
            if (stopRequested_.load(std::memory_order_acquire)) break;
        }

        GpuFrame frame;
        if (!vsrInputQueue_->pop(frame, 100)) continue;

        if (frame.deviceLost) {
            spdlog::warn("[VSRPipeline::VSR] Device-lost frame, skipping");
            continue;
        }

        VSRState currentVSR = vsrState_.load(std::memory_order_acquire);

        if (currentVSR == VSRState::CircuitOpen) {
            auto elapsed = std::chrono::steady_clock::now() - circuitOpenTime_;
            auto cooldown = std::chrono::duration<double>(config_.vsrCooldownSeconds);
            if (elapsed >= cooldown) {
                startProbing();
                currentVSR = vsrState_.load(std::memory_order_acquire);
            }
        }

        if (!vsrNode_ || currentVSR == VSRState::CircuitOpen) {
            outputQueue_->push(std::move(frame));
            vsrBypassedFrames_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        if (vramManager_) {
            auto allocResult = vramManager_->requestAllocation(
                vsrFrameVRAM_, config_.vramAllocationTimeoutMs);
            if (allocResult.hasError()) {
                spdlog::warn("[VSRPipeline::VSR] VRAM budget denied, bypassing frame");
                outputQueue_->push(std::move(frame));
                vsrBypassedFrames_.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
        }

        auto t0 = std::chrono::steady_clock::now();
        auto result = vsrNode_->process(frame);
        auto t1 = std::chrono::steady_clock::now();

        if (vramManager_) {
            vramManager_->release(vsrFrameVRAM_);
        }

        double inferenceMs =
            std::chrono::duration<double, std::milli>(t1 - t0).count();
        lastVSRInferenceMs_.store(inferenceMs, std::memory_order_relaxed);

        if (result.hasError()) {
            spdlog::warn("[VSRPipeline::VSR] Inference error ({}), bypassing frame",
                         static_cast<int>(result.error()));
            outputQueue_->push(std::move(frame));
            vsrBypassedFrames_.fetch_add(1, std::memory_order_relaxed);
            continue;
        }

        updateCircuitBreaker(inferenceMs);

        if (vsrState_.load(std::memory_order_acquire) == VSRState::Probing) {
            handleProbeResult(inferenceMs);
        }

        auto outFrame = std::move(result.value());

        if (syncClock_) {
            syncClock_->setVideoClock(frame.timestamp);
        }

        outputQueue_->push(std::move(outFrame));
        vsrProcessedFrames_.fetch_add(1, std::memory_order_relaxed);
    }

    if (vsrNode_) {
        auto remaining = vsrNode_->flush();
        if (remaining.hasValue()) {
            for (auto& f : remaining.value()) {
                outputQueue_->push(std::move(f));
            }
        }
    }

    spdlog::debug("[VSRPipeline::VSR] Thread exiting");
}

// ============================================================================
// Render Stage
// ============================================================================

void RealtimeVSRPipeline::renderStage() {
    spdlog::debug("[VSRPipeline::Render] Thread started");

    while (!stopRequested_.load(std::memory_order_acquire)) {
        if (paused_.load(std::memory_order_acquire)) {
            std::unique_lock lock(pauseMutex_);
            pauseCv_.wait(lock, [this] {
                return !paused_.load(std::memory_order_acquire) ||
                       stopRequested_.load(std::memory_order_acquire);
            });
            if (stopRequested_.load(std::memory_order_acquire)) break;
        }

        GpuFrame frame;
        if (!outputQueue_->pop(frame, 100)) continue;

        if (frame.deviceLost) {
            spdlog::warn("[VSRPipeline::Render] Device-lost frame, skipping");
            continue;
        }

        if (renderBridge_) {
            renderBridge_->presentFrame(frame);
        }
    }

    if (renderBridge_) {
        renderBridge_->reset();
    }

    spdlog::debug("[VSRPipeline::Render] Thread exiting");
}

// ============================================================================
// Circuit Breaker
// ============================================================================

void RealtimeVSRPipeline::updateCircuitBreaker(double inferenceMs) {
    VSRState state = vsrState_.load(std::memory_order_acquire);

    if (state == VSRState::Active) {
        if (inferenceMs > config_.vsrSlowThresholdMs) {
            ++consecutiveSlowFrames_;
            if (consecutiveSlowFrames_ >= config_.vsrSlowFrameCount) {
                VSRState prev = VSRState::Active;
                if (vsrState_.compare_exchange_strong(prev, VSRState::CircuitOpen,
                        std::memory_order_acq_rel)) {
                    circuitOpenTime_ = std::chrono::steady_clock::now();
                    spdlog::warn("[VSRPipeline] Circuit breaker OPEN "
                                 "({} consecutive frames > {:.1f}ms, last={:.1f}ms)",
                                 consecutiveSlowFrames_,
                                 config_.vsrSlowThresholdMs, inferenceMs);
                    if (eventBus_) {
                        Event evt;
                        evt.type = EventType::Error;
                        evt.timestamp = 0.0;
                        evt.payload = ErrorPayload{
                            PlayerError::DecodeError,
                            "VSR circuit breaker opened: inference too slow"};
                        eventBus_->publish(evt);
                    }
                }
                consecutiveSlowFrames_ = 0;
            }
        } else {
            consecutiveSlowFrames_ = 0;
        }
    }
}

void RealtimeVSRPipeline::startProbing() {
    VSRState prev = VSRState::CircuitOpen;
    if (vsrState_.compare_exchange_strong(prev, VSRState::Probing,
            std::memory_order_acq_rel)) {
        spdlog::info("[VSRPipeline] Probing VSR after cooldown");
    }
}

void RealtimeVSRPipeline::handleProbeResult(double inferenceMs) {
    if (inferenceMs <= config_.vsrSlowThresholdMs) {
        VSRState prev = VSRState::Probing;
        if (vsrState_.compare_exchange_strong(prev, VSRState::Active,
                std::memory_order_acq_rel)) {
            spdlog::info("[VSRPipeline] VSR recovered (probe={:.1f}ms)", inferenceMs);
            if (eventBus_) {
                Event evt;
                evt.type = EventType::ResolutionChanged;
                evt.timestamp = 0.0;
                evt.payload = ResolutionPayload{0, 0};
                eventBus_->publish(evt);
            }
        }
    } else {
        VSRState prev = VSRState::Probing;
        if (vsrState_.compare_exchange_strong(prev, VSRState::CircuitOpen,
                std::memory_order_acq_rel)) {
            circuitOpenTime_ = std::chrono::steady_clock::now();
            spdlog::warn("[VSRPipeline] VSR probe failed ({:.1f}ms), "
                         "staying bypassed", inferenceMs);
        }
    }
}

// ============================================================================
// Helpers
// ============================================================================

void RealtimeVSRPipeline::joinAllThreads() {
    if (decodeThread_.joinable()) decodeThread_.join();
    if (vsrThread_.joinable()) vsrThread_.join();
    if (renderThread_.joinable()) renderThread_.join();
}

void RealtimeVSRPipeline::publishError(PlayerError err, const std::string& message) {
    if (eventBus_) {
        Event evt;
        evt.type = EventType::Error;
        evt.timestamp = 0.0;
        evt.payload = ErrorPayload{err, message};
        eventBus_->publish(evt);
    }
}

uint64_t RealtimeVSRPipeline::vsrDroppedFrames() const {
    return vsrDroppedFrames_.load(std::memory_order_relaxed);
}

uint64_t RealtimeVSRPipeline::vsrProcessedFrames() const {
    return vsrProcessedFrames_.load(std::memory_order_relaxed);
}

uint64_t RealtimeVSRPipeline::vsrBypassedFrames() const {
    return vsrBypassedFrames_.load(std::memory_order_relaxed);
}

double RealtimeVSRPipeline::lastVSRInferenceMs() const {
    return lastVSRInferenceMs_.load(std::memory_order_relaxed);
}

} // namespace hlplayer
