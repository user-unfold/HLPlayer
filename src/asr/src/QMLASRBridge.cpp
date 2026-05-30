#include <hlplayer/QMLASRBridge.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/ASRPipeline.h>
#include <hlplayer/IAudioDecoder.h>

#include <QMetaObject>
#include <QStandardPaths>
#include <QtGlobal>

#include <spdlog/spdlog.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <variant>

namespace hlplayer {
namespace asr {

// Default ASR configuration constants (overridable via ASRConfig)
namespace defaults {
    constexpr int kGpuDevice = 1;           // Discrete GPU (device 1 = NVIDIA on dual-GPU laptops)
    constexpr int kMaxSegmentLengthMs = 3000;
    constexpr int kAudioContextMs = 500;
    constexpr float kVadThreshold = 0.3f;
    constexpr size_t kMaxQueueSize = 500;
}

struct QMLASRBridge::Impl {
    bool enabled = false;
    QString currentSubtitleText;
    QString translatedSubtitleText;
    bool isRunning = false;
    int asrState = static_cast<int>(ASRState::Idle);
    QString language = "auto";
    int audioSource = static_cast<int>(AudioSourceType::VideoTrack);
    int displayMode = static_cast<int>(SubtitleDisplayMode::Bilingual);
    QVariantList modelList;
    int fontSize = 18;
    QString fontColor = "#FFFFFF";
    QString modelDir;
    EventBus* eventBus = nullptr;
    int subtitleSubscriptionId = -1;
    int asrStateSubscriptionId = -1;
    int playerStateSubscriptionId = -1;
    QTimer* eventDispatchTimer = nullptr;
    std::unique_ptr<ASRPipeline> pipeline;
    QObject* qmlPlayer = nullptr;
    QMLASRBridge* owner = nullptr;

    bool modelReady_ = false;
    QString modelLoadingStatus_;
    std::string phase2ModelPath;
    bool audioCallbackRegistered = false;
    std::chrono::steady_clock::time_point pipelineStartTime_;
    std::chrono::steady_clock::time_point lastSubtitleUpdate_;
    static constexpr int kMinDisplayIntervalMs = 600;
    std::atomic<bool> destroyed_{false};
};

QMLASRBridge::QMLASRBridge(EventBus* eventBus, QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->eventBus = eventBus;
    impl_->eventDispatchTimer = new QTimer(this);
    impl_->eventDispatchTimer->setInterval(50);
    connect(impl_->eventDispatchTimer, &QTimer::timeout, this, [this]() {
        if (impl_->eventBus) {
            impl_->eventBus->dispatch();
        }
    });
    impl_->eventDispatchTimer->start();

    setupEventBusSubscription();
    spdlog::info("HLPlayer::QMLASRBridge constructed");
}

QMLASRBridge::~QMLASRBridge() {
    impl_->destroyed_.store(true);

    if (impl_->pipeline) {
        impl_->pipeline->stop();
        impl_->pipeline->shutdown();
        impl_->pipeline.reset();
    }
    if (impl_->eventDispatchTimer) {
        impl_->eventDispatchTimer->stop();
    }
    if (impl_->subtitleSubscriptionId >= 0 && impl_->eventBus) {
        impl_->eventBus->unsubscribe(impl_->subtitleSubscriptionId);
    }
    if (impl_->asrStateSubscriptionId >= 0 && impl_->eventBus) {
        impl_->eventBus->unsubscribe(impl_->asrStateSubscriptionId);
    }
    if (impl_->playerStateSubscriptionId >= 0 && impl_->eventBus) {
        impl_->eventBus->unsubscribe(impl_->playerStateSubscriptionId);
    }
    spdlog::info("HLPlayer::QMLASRBridge destructed");
}

void QMLASRBridge::setupEventBusSubscription() {
    if (!impl_->eventBus) {
        spdlog::warn("QMLASRBridge: EventBus not set - cannot subscribe to events");
        return;
    }

    impl_->subtitleSubscriptionId = impl_->eventBus->subscribe(
        EventType::SubtitleReady,
        [this](const Event& event) {
            if (event.type == EventType::SubtitleReady) {
                const auto& payload = std::get<SubtitleReadyPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, payload]() {
                        handleSubtitleReadyEvent(
                            QString::fromUtf8(payload.text),
                            QString::fromUtf8(payload.translation),
                            payload.startTime,
                            payload.endTime,
                            QString::fromUtf8(payload.language),
                            payload.sequenceId
                        );
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );

    impl_->asrStateSubscriptionId = impl_->eventBus->subscribe(
        EventType::ASRStateChanged,
        [this](const Event& event) {
            if (event.type == EventType::ASRStateChanged) {
                const auto& payload = std::get<ASRStateChangedPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, payload]() {
                        handleASRStateChangedEvent(
                            static_cast<int>(payload.oldState),
                            static_cast<int>(payload.newState)
                        );
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );

    impl_->playerStateSubscriptionId = impl_->eventBus->subscribe(
        EventType::StateChanged,
        [this](const Event& event) {
            if (event.type == EventType::StateChanged) {
                const auto& payload = std::get<StateChangedPayload>(event.payload);
                if (payload.newState == PlayerState_Playing) {
                    QMetaObject::invokeMethod(this, [this]() {
                        ensureAudioCallbackRegistered();
                    }, Qt::QueuedConnection);
                }
            }
        }
    );
}

void QMLASRBridge::ensureAudioCallbackRegistered() {
    if (!impl_->qmlPlayer || impl_->audioCallbackRegistered) return;
    if (!impl_->pipeline) {
        impl_->pipeline = std::make_unique<ASRPipeline>();
        ASRConfig config;
        config.maxQueueSize = 500;
        impl_->pipeline->initialize(config);
    }
    QMetaObject::invokeMethod(impl_->qmlPlayer, "setAudioFrameCallback",
        Qt::DirectConnection,
        Q_ARG(QVariant, QVariant::fromValue(reinterpret_cast<quintptr>(impl_->pipeline.get())))
    );
    impl_->audioCallbackRegistered = true;
    spdlog::info("QMLASRBridge: audio callback registered on playback start");
}

std::string QMLASRBridge::findBestAvailableModel(const std::vector<std::string>& priority) const {
    if (impl_->modelDir.isEmpty()) return {};
    auto models = ASRPipeline::listAvailableModels(impl_->modelDir.toStdString());
    for (const auto& name : priority) {
        for (const auto& m : models) {
            if (m.available && m.name == name) return m.path;
        }
    }
    for (const auto& m : models) {
        if (m.available) return m.path;
    }
    return {};
}

void QMLASRBridge::beginPhasedPreload() {
    if (impl_->modelReady_) return;
    if (impl_->modelDir.isEmpty()) return;

    impl_->phase2ModelPath = findBestAvailableModel({"large-v3-turbo", "medium", "small", "base", "tiny"});
    startPhase2Upgrade();
}

void QMLASRBridge::startPhase2Upgrade() {
    if (impl_->phase2ModelPath.empty()) return;
    if (!impl_->pipeline) {
        impl_->pipeline = std::make_unique<ASRPipeline>();
        ASRConfig config;
        config.maxQueueSize = 500;
        impl_->pipeline->initialize(config);
    }

    impl_->modelLoadingStatus_ = QStringLiteral("智能字幕准备中...");
    emit modelLoadingStatusChanged();

    ASRConfig preloadConfig;
    preloadConfig.maxQueueSize = defaults::kMaxQueueSize;
    preloadConfig.useGPU = true;
    preloadConfig.gpuDevice = defaults::kGpuDevice;
    preloadConfig.language = impl_->language.toStdString();
    preloadConfig.modelPath = impl_->phase2ModelPath;
    preloadConfig.maxSegmentLengthMs = defaults::kMaxSegmentLengthMs;
    preloadConfig.audioContextMs = defaults::kAudioContextMs;

    spdlog::info("QMLASRBridge: preload starting (best available model)");

    QTimer::singleShot(0, this, [this, preloadConfig]() {
        if (!impl_->pipeline) return;

        impl_->pipeline->preloadModel(preloadConfig, [this](bool success) {
            QMetaObject::invokeMethod(this, [this, success]() {
                if (impl_->destroyed_.load()) return;
                if (success) {
                    impl_->modelReady_ = true;
                    impl_->modelLoadingStatus_.clear();
                    emit modelReadyChanged();
                    emit modelLoadingStatusChanged();
                    spdlog::info("QMLASRBridge: model ready");
                } else {
                    impl_->modelLoadingStatus_ = QStringLiteral("模型加载失败");
                    emit modelLoadingStatusChanged();
                    spdlog::error("QMLASRBridge: model preload failed");
                }
            }, Qt::QueuedConnection);
        });
    });
}

void QMLASRBridge::handleSubtitleReadyEvent(const QString& text, const QString& translation,
                                            double startTime, double endTime,
                                            const QString& language, int sequenceId) {
    Q_UNUSED(language)
    Q_UNUSED(sequenceId)

    if (text.isEmpty()) return;

    // Throttle display updates to prevent flickering during backlog processing
    auto now = std::chrono::steady_clock::now();
    auto msSinceLastUpdate = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - impl_->lastSubtitleUpdate_).count();
    if (msSinceLastUpdate < Impl::kMinDisplayIntervalMs) {
        return;
    }

    impl_->lastSubtitleUpdate_ = now;
    impl_->currentSubtitleText = text;
    emit currentSubtitleTextChanged();

    if (!translation.isEmpty()) {
        impl_->translatedSubtitleText = translation;
        emit translatedSubtitleTextChanged();
    }

    spdlog::info("QMLASRBridge: displayed subtitle '{}' (start={:.1f}s, end={:.1f}s)",
                 text.toUtf8().toStdString(), startTime, endTime);
}

void QMLASRBridge::handleASRStateChangedEvent(int oldState, int newState) {
    spdlog::info("QMLASRBridge: ASR state changed from {} to {}", oldState, newState);

    impl_->asrState = newState;
    emit asrStateChanged();

    impl_->isRunning = (newState == static_cast<int>(ASRState::Running));
    emit isRunningChanged();
}

bool QMLASRBridge::enabled() const {
    return impl_->enabled;
}

void QMLASRBridge::setEnabled(bool enabled) {
    if (impl_->enabled == enabled) return;
    impl_->enabled = enabled;
    emit enabledChanged();
    spdlog::info("QMLASRBridge: setEnabled - enabled={}", enabled);

    if (!impl_->eventBus) return;

    if (enabled) {
        startPipeline();
    } else {
        stopPipeline();
    }
}

bool QMLASRBridge::modelReady() const {
    return impl_->modelReady_;
}

QString QMLASRBridge::modelLoadingStatus() const {
    return impl_->modelLoadingStatus_;
}

void QMLASRBridge::startPipeline() {
    if (impl_->pipeline && impl_->pipeline->state() == ASRState::Running) {
        spdlog::warn("QMLASRBridge: pipeline already running");
        return;
    }

    std::string modelPath;
    if (impl_->modelDir.isEmpty()) {
        QString msg = "No model directory configured.";
        spdlog::error("QMLASRBridge: {}", msg.toStdString());
        emit errorOccurred(msg);
        impl_->enabled = false;
        emit enabledChanged();
        return;
    }

    // Use the preloaded model path if available, otherwise find best available
    if (!impl_->phase2ModelPath.empty()) {
        modelPath = impl_->phase2ModelPath;
    } else {
        modelPath = findBestAvailableModel({"large-v3-turbo", "small", "medium", "base", "tiny"});
    }

    if (modelPath.empty()) {
        QString msg = QString("No whisper model found in %1.").arg(impl_->modelDir);
        spdlog::error("QMLASRBridge: {}", msg.toStdString());
        emit errorOccurred(msg);
        impl_->enabled = false;
        emit enabledChanged();
        return;
    }

    if (!impl_->pipeline) {
        impl_->pipeline = std::make_unique<ASRPipeline>();
    }

    ASRConfig config;
    config.modelPath = modelPath;
    config.language = impl_->language.toStdString();
    config.audioSource = static_cast<AudioSourceType>(impl_->audioSource);
    config.enableTranslation = false;
    config.useGPU = true;
    config.gpuDevice = defaults::kGpuDevice;
    config.maxSegmentLengthMs = defaults::kMaxSegmentLengthMs;
    config.audioContextMs = defaults::kAudioContextMs;
    config.vadThreshold = defaults::kVadThreshold;
    config.maxQueueSize = defaults::kMaxQueueSize;

    impl_->pipeline->setSubtitleCallback([this](const std::vector<SubtitleSegment>& segments) {
        if (segments.empty()) return;
        const auto& seg = segments.back();
        spdlog::info("QMLASRBridge: subtitle callback fired, text='{}', lang='{}'", seg.text, seg.language);
        if (impl_->eventBus) {
            SubtitleReadyPayload payload;
            payload.text = seg.text;
            payload.translation = seg.translation;
            payload.startTime = seg.startTime;
            payload.endTime = seg.endTime;
            payload.language = seg.language;
            payload.sequenceId = seg.sequenceId;
            impl_->eventBus->publish(Event{EventType::SubtitleReady, 0.0, payload});
        }
    });

    impl_->pipeline->setStateCallback([this](ASRState oldState, ASRState newState) {
        if (impl_->eventBus) {
            ASRStateChangedPayload payload{oldState, newState};
            impl_->eventBus->publish(Event{EventType::ASRStateChanged, 0.0, payload});
        }
    });

    if (!impl_->pipeline->initialize(config)) {
        QString msg = "Failed to initialize ASR pipeline. Check model file.";
        spdlog::error("QMLASRBridge: {}", msg.toStdString());
        emit errorOccurred(msg);
        impl_->enabled = false;
        emit enabledChanged();
        return;
    }

    if (!impl_->pipeline->start()) {
        QString msg = "Failed to start ASR pipeline.";
        spdlog::error("QMLASRBridge: {}", msg.toStdString());
        emit errorOccurred(msg);
        impl_->enabled = false;
        emit enabledChanged();
        return;
    }

    impl_->pipelineStartTime_ = std::chrono::steady_clock::now();
    impl_->lastSubtitleUpdate_ = {};

    spdlog::info("QMLASRBridge: ASR pipeline started with model {}", modelPath);
}

void QMLASRBridge::stopPipeline() {
    if (!impl_->pipeline) return;

    if (impl_->qmlPlayer) {
        QMetaObject::invokeMethod(impl_->qmlPlayer, "setAudioFrameCallback",
            Qt::DirectConnection,
            Q_ARG(QVariant, QVariant(0))
        );
        impl_->audioCallbackRegistered = false;
    }

    impl_->pipeline->stop();
    impl_->pipeline->reset();
    impl_->pipeline->shutdown();

    impl_->currentSubtitleText = "";
    emit currentSubtitleTextChanged();
    impl_->translatedSubtitleText = "";
    emit translatedSubtitleTextChanged();

    spdlog::info("QMLASRBridge: ASR pipeline stopped");
}

QString QMLASRBridge::currentSubtitleText() const {
    return impl_->currentSubtitleText;
}

QString QMLASRBridge::translatedSubtitleText() const {
    return impl_->translatedSubtitleText;
}

bool QMLASRBridge::isRunning() const {
    return impl_->isRunning;
}

int QMLASRBridge::asrState() const {
    return impl_->asrState;
}

QString QMLASRBridge::language() const {
    return impl_->language;
}

int QMLASRBridge::audioSource() const {
    return impl_->audioSource;
}

int QMLASRBridge::displayMode() const {
    return impl_->displayMode;
}

QVariantList QMLASRBridge::modelList() const {
    return impl_->modelList;
}

void QMLASRBridge::setLanguage(const QString& language) {
    if (impl_->language == language) return;
    impl_->language = language;
    emit languageChanged();
}

void QMLASRBridge::setAudioSource(int source) {
    if (impl_->audioSource == source) return;
    impl_->audioSource = source;
    emit audioSourceChanged();
}

void QMLASRBridge::setDisplayMode(int mode) {
    if (impl_->displayMode == mode) return;
    impl_->displayMode = mode;
    emit displayModeChanged();
}

void QMLASRBridge::toggleASR() {
    setEnabled(!impl_->enabled);
}

void QMLASRBridge::setModel(const QString& modelPath) {
    Q_UNUSED(modelPath)
    spdlog::info("QMLASRBridge: setModel called - delegation to PlayerFacade pending");
}

void QMLASRBridge::exportSRT(const QString& filePath) {
    if (filePath.isEmpty()) {
        spdlog::warn("QMLASRBridge: exportSRT called with empty path");
        return;
    }

    if (!impl_->pipeline) {
        spdlog::warn("QMLASRBridge: cannot export SRT — pipeline not created");
        emit errorOccurred("Cannot export subtitles: ASR not started");
        return;
    }

    const std::string path = filePath.toStdString();
    if (impl_->pipeline->subtitleManager().exportSRTFile(path)) {
        spdlog::info("QMLASRBridge: SRT exported to {}", path);
    } else {
        spdlog::error("QMLASRBridge: failed to export SRT to {}", path);
        emit errorOccurred("Failed to export subtitles");
    }
}

int QMLASRBridge::fontSize() const {
    return impl_->fontSize;
}

void QMLASRBridge::setFontSize(int size) {
    if (impl_->fontSize == size) return;
    impl_->fontSize = size;
    emit fontSizeChanged();
    spdlog::info("QMLASRBridge: setFontSize - fontSize={}", size);
}

QString QMLASRBridge::fontColor() const {
    return impl_->fontColor;
}

void QMLASRBridge::setFontColor(const QString& color) {
    if (impl_->fontColor == color) return;
    impl_->fontColor = color;
    emit fontColorChanged();
    spdlog::info("QMLASRBridge: setFontColor - fontColor={}", color.toStdString());
}

void QMLASRBridge::initFromPlayer(QObject* qmlPlayer) {
    if (!qmlPlayer) return;

    impl_->qmlPlayer = qmlPlayer;

    quintptr ptr = 0;
    QMetaObject::invokeMethod(qmlPlayer, "eventBusPointer",
        Qt::DirectConnection,
        Q_RETURN_ARG(quintptr, ptr));

    auto* eventBus = reinterpret_cast<EventBus*>(ptr);
    if (eventBus && !impl_->eventBus) {
        impl_->eventBus = eventBus;
        setupEventBusSubscription();
        spdlog::info("QMLASRBridge: EventBus connected from player");
    }
}

void QMLASRBridge::setModelDirectory(const QString& dir) {
    impl_->modelDir = dir;
    auto models = ASRPipeline::listAvailableModels(dir.toStdString());
    impl_->modelList.clear();
    for (const auto& m : models) {
        QVariantMap item;
        item["name"] = QString::fromStdString(m.name);
        item["path"] = QString::fromStdString(m.path);
        item["available"] = m.available;
        impl_->modelList.append(item);
    }
    emit modelListChanged();
    spdlog::info("QMLASRBridge: model directory set to {}, found {} models",
                 dir.toStdString(), models.size());

    beginPhasedPreload();
}

}
} 
