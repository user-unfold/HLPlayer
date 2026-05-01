#include "QMLPlayer.h"
#include <hlplayer/FFPlayer.h>
#include <hlplayer/MediaPlayer.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/DirectStreamResolver.h>
#include <ExtractorFactory.h>
#include <VulkanVideoSink.h>
#include <SDLAudioRenderer.h>
#include <SubtitleRenderer.h>

#include <QtGlobal>
#include <QUrl>
#include <QDateTime>

#include <spdlog/spdlog.h>

#include <atomic>
#include <cmath>
#include <memory>
#include <variant>

namespace hlplayer {
namespace qml {

struct QMLPlayer::Impl {
    std::unique_ptr<render::VulkanVideoSink> videoSink;
    std::unique_ptr<MediaPlayer> mediaPlayer;
    std::unique_ptr<subtitle::SubtitleRenderer> subtitleRenderer;
    QString source;
    double volume = 1.0;
    double duration = 0.0;
    double fpsValue = 0.0;
    double playbackRateValue = 1.0;
    bool lowLatencyModeValue = false;
    double seekTarget_ = -1.0;
    qint64 seekStabilizeStart_ = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int reconnectAttemptValue = 0;
    QString connectionStateValue = "connected";
    qreal streamBitrateValue = 0.0;
    int bufferDurationValue = 0;
    int droppedFramesValue = 0;
    int stateChangeSubscriptionId = -1;
    int errorSubscriptionId = -1;
    int resolutionSubscriptionId = -1;
    int reconnectSubscriptionId = -1;
    QTimer* positionTimer = nullptr;
    QTimer* eventDispatchTimer = nullptr;
    QTimer* statsTimer = nullptr;
    QMLPlayer* owner = nullptr;
};

QMLPlayer::QMLPlayer(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {
    impl_->videoSink = std::make_unique<render::VulkanVideoSink>();
    impl_->subtitleRenderer = std::make_unique<subtitle::SubtitleRenderer>();

    auto demuxer = extractor::createFFmpegDemuxer();
    auto decoder = extractor::createFFmpegVideoDecoder();
    auto audioDecoder = extractor::createFFmpegAudioDecoder();
    auto audioRenderer = std::make_unique<render::SDLAudioRenderer>();

    auto ffPlayer = std::make_unique<FFPlayer>();
    ffPlayer->setDemuxer(std::move(demuxer));
    ffPlayer->setVideoDecoder(std::move(decoder));
    ffPlayer->setAudioDecoder(std::move(audioDecoder));
    ffPlayer->setAudioRenderer(std::move(audioRenderer));
    ffPlayer->setVideoSink(impl_->videoSink.get());

    impl_->mediaPlayer = std::make_unique<MediaPlayer>(std::move(ffPlayer));

    impl_->owner = this;

    impl_->positionTimer = new QTimer(this);
    impl_->positionTimer->setInterval(50);
    connect(impl_->positionTimer, &QTimer::timeout, this, [this]() {
        double liveDuration = impl_->mediaPlayer->duration();
        if (liveDuration > 0.0 && !qFuzzyCompare(impl_->duration, liveDuration)) {
            impl_->duration = liveDuration;
            emit durationChanged();
        }
        double liveFps = impl_->mediaPlayer->player()->getFps();
        if (!qFuzzyCompare(impl_->fpsValue, liveFps) && liveFps > 0.0) {
            impl_->fpsValue = liveFps;
            emit fpsChanged();
        }
        // Always emit positionChanged so the UI stays in sync with backend
        emit positionChanged();
    });

    impl_->eventDispatchTimer = new QTimer(this);
    impl_->eventDispatchTimer->setInterval(30);
    connect(impl_->eventDispatchTimer, &QTimer::timeout, this, [this]() {
        impl_->mediaPlayer->eventBus().dispatch();
    });
    impl_->eventDispatchTimer->start();

    impl_->statsTimer = new QTimer(this);
    impl_->statsTimer->setInterval(500);
    connect(impl_->statsTimer, &QTimer::timeout, this, [this]() {
        emit streamBitrateChanged();
        emit bufferDurationChanged();
        emit droppedFramesChanged();
    });

    setupEventBusSubscription();
    spdlog::info("HLPlayer::QMLPlayer constructed");
}


QMLPlayer::~QMLPlayer() {
    if (impl_->eventDispatchTimer) {
        impl_->eventDispatchTimer->stop();
    }
    if (impl_->positionTimer) {
        impl_->positionTimer->stop();
    }
    if (impl_->statsTimer) {
        impl_->statsTimer->stop();
    }
    if (impl_->stateChangeSubscriptionId >= 0) {
        impl_->mediaPlayer->eventBus().unsubscribe(impl_->stateChangeSubscriptionId);
    }
    if (impl_->errorSubscriptionId >= 0) {
        impl_->mediaPlayer->eventBus().unsubscribe(impl_->errorSubscriptionId);
    }
    if (impl_->resolutionSubscriptionId >= 0) {
        impl_->mediaPlayer->eventBus().unsubscribe(impl_->resolutionSubscriptionId);
    }
    if (impl_->reconnectSubscriptionId >= 0) {
        impl_->mediaPlayer->eventBus().unsubscribe(impl_->reconnectSubscriptionId);
    }
    spdlog::info("HLPlayer::QMLPlayer destructed");
}

void QMLPlayer::setupEventBusSubscription() {
    impl_->stateChangeSubscriptionId = impl_->mediaPlayer->eventBus().subscribe(
        EventType::StateChanged,
        [this](const Event& event) {
            if (event.type == EventType::StateChanged) {
                const auto& payload = std::get<StateChangedPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, payload]() {
                        handleStateChangedEvent(
                            static_cast<int>(payload.oldState),
                            static_cast<int>(payload.newState)
                        );
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );

    impl_->errorSubscriptionId = impl_->mediaPlayer->eventBus().subscribe(
        EventType::Error,
        [this](const Event& event) {
            if (event.type == EventType::Error) {
                const auto& payload = std::get<ErrorPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, payload]() {
                        handleErrorEvent(
                            static_cast<int>(payload.error),
                            QString::fromStdString(payload.message)
                        );
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );

    impl_->resolutionSubscriptionId = impl_->mediaPlayer->eventBus().subscribe(
        EventType::ResolutionChanged,
        [this](const Event& event) {
            if (event.type == EventType::ResolutionChanged) {
                const auto& payload = std::get<ResolutionPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, w = payload.width, h = payload.height]() {
                        impl_->videoWidth = static_cast<int>(w);
                        impl_->videoHeight = static_cast<int>(h);
                        emit videoResolutionChanged();
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );

    impl_->reconnectSubscriptionId = impl_->mediaPlayer->eventBus().subscribe(
        EventType::ReconnectStateChanged,
        [this](const Event& event) {
            if (event.type == EventType::ReconnectStateChanged) {
                const auto& payload = std::get<ReconnectStateChangedPayload>(event.payload);
                QMetaObject::invokeMethod(
                    this,
                    [this, attempt = payload.attempt, delay = payload.delaySeconds, state = QString::fromStdString(payload.state)]() {
                        impl_->reconnectAttemptValue = attempt;
                        impl_->connectionStateValue = state;
                        spdlog::info("QMLPlayer: reconnect state changed - attempt={}, delay={}s, state={}",
                                     attempt, delay, state.toStdString());
                        emit reconnectAttemptChanged();
                        emit connectionStateChanged();
                    },
                    Qt::QueuedConnection
                );
            }
        }
    );
}

void QMLPlayer::handleStateChangedEvent(int oldState, int newState) {
    spdlog::info("QMLPlayer: State changed from {} to {}",
                 oldState, newState);

    if (newState == static_cast<int>(PlayerState_Prepared) ||
        newState == static_cast<int>(PlayerState_Playing)) {
        double newDuration = impl_->mediaPlayer->duration();
        if (newDuration > 0.0 && !qFuzzyCompare(impl_->duration, newDuration)) {
            impl_->duration = newDuration;
            spdlog::info("QMLPlayer: duration updated to {:.3f}s", newDuration);
            emit durationChanged();
        }
    }

    if (newState == static_cast<int>(PlayerState_Error) ||
        newState == static_cast<int>(PlayerState_End)) {
        impl_->positionTimer->stop();
        impl_->statsTimer->stop();
    }

    if (newState == static_cast<int>(PlayerState_End)) {
        // Don't snap position on End state.  The backend position naturally
        // reflects the end of the file.  Let the position timer handle updates.
    }

    if (newState == static_cast<int>(PlayerState_Idle)) {
        impl_->duration = 0.0;
        emit durationChanged();
    }

    emit stateChanged();
    emit isPlayingChanged();
    emit isPausedChanged();
}

void QMLPlayer::handleErrorEvent(int errorCode, const QString& errorMessage) {
    spdlog::warn("QMLPlayer: Error occurred - Code: {}, Message: {}",
                 errorCode, errorMessage.toStdString());

    // Clear seek override on error to prevent UI getting stuck at failed seek position
    if (impl_->seekTarget_ >= 0.0) {
        impl_->seekTarget_ = -1.0;
        impl_->seekStabilizeStart_ = 0;
    }

    emit errorChanged();
}

QString QMLPlayer::source() const {
    return impl_->source;
}

void QMLPlayer::setSource(const QString& source) {
    impl_->source = source;

    QString normalizedSource = source;
    QUrl url;
    if (!source.isEmpty()) {
        url = QUrl(source);
        if (!url.isValid()) {
            url = QUrl::fromUserInput(source);
        }

        if (url.isValid() && url.isLocalFile()) {
            const QString localPath = url.toLocalFile();
            if (!localPath.isEmpty()) {
                normalizedSource = localPath;
            }
        }
    }

    impl_->positionTimer->stop();
    impl_->seekTarget_ = -1.0;
    impl_->seekStabilizeStart_ = 0;
    impl_->duration = 0.0;

    impl_->subtitleRenderer->reset();

    impl_->mediaPlayer->open(normalizedSource.toStdString());

    if (url.isLocalFile()) {
        auto discovered = impl_->subtitleRenderer->autoDiscover(normalizedSource.toStdString());
        if (!discovered.empty()) {
            impl_->subtitleRenderer->loadFile(discovered);
            spdlog::info("QMLPlayer: auto-loaded subtitle '{}'", discovered);
        }
    }

    QMetaObject::invokeMethod(this, [this]() {
        emit sourceChanged();
        emit positionChanged();
        emit durationChanged();
        emit subtitleChanged();
    }, Qt::QueuedConnection);
}

int QMLPlayer::state() const {
    return static_cast<int>(impl_->mediaPlayer->state());
}

double QMLPlayer::volume() const {
    return impl_->volume;
}

void QMLPlayer::setVolume(double volume) {
    if (qFuzzyCompare(impl_->volume, volume)) return;
    impl_->volume = volume;

    auto result = impl_->mediaPlayer->player()->setVolume(volume);
    if (result.hasError()) {
        spdlog::warn("QMLPlayer::setVolume failed: {}",
                     static_cast<int>(result.error()));
    }

    QMetaObject::invokeMethod(this, [this]() { emit volumeChanged(); },
                              Qt::QueuedConnection);
}

double QMLPlayer::position() const {
    if (impl_->seekTarget_ >= 0.0) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        qint64 elapsed = now - impl_->seekStabilizeStart_;

        // Safety timeout: force-clear after 2 seconds
        if (elapsed > 2000) {
            impl_->seekTarget_ = -1.0;
            return impl_->mediaPlayer->position();
        }

        // During first 250ms, return seek target for instant UI feedback
        if (elapsed < 250) {
            return impl_->seekTarget_;
        }

        // After 250ms, return backend position — FFPlayer::getPosition() now
        // uses positionFloor_ to return the seek target during convergence,
        // so the backend is reliable without a separate convergence check here.
        impl_->seekTarget_ = -1.0;
        return impl_->mediaPlayer->position();
    }
    return impl_->mediaPlayer->position();
}

double QMLPlayer::duration() const {
    return impl_->duration;
}

QString QMLPlayer::error() const {
    return {};
}

bool QMLPlayer::isPlaying() const {
    return impl_->mediaPlayer->state() == PlayerState_Playing;
}

bool QMLPlayer::isPaused() const {
    return impl_->mediaPlayer->state() == PlayerState_Paused;
}

void QMLPlayer::play() {
    impl_->mediaPlayer->play();
    impl_->positionTimer->start();
    impl_->statsTimer->start();
    double liveDuration = impl_->mediaPlayer->duration();
    if (liveDuration > 0.0 && !qFuzzyCompare(impl_->duration, liveDuration)) {
        impl_->duration = liveDuration;
        QMetaObject::invokeMethod(this, [this]() { emit durationChanged(); },
                                   Qt::QueuedConnection);
    }
}

void QMLPlayer::pause() {
    impl_->mediaPlayer->pause();
    impl_->positionTimer->stop();
    impl_->statsTimer->stop();
}

void QMLPlayer::stop() {
    impl_->seekTarget_ = -1.0;
    impl_->seekStabilizeStart_ = 0;
    impl_->mediaPlayer->stop();
    impl_->positionTimer->stop();
    impl_->statsTimer->stop();
}

void QMLPlayer::seek(double seconds) {
    impl_->seekTarget_ = seconds;
    impl_->seekStabilizeStart_ = QDateTime::currentMSecsSinceEpoch();
    emit positionChanged();
    impl_->mediaPlayer->seek(seconds);
    if (!impl_->positionTimer->isActive())
        impl_->positionTimer->start();
}

hlplayer::render::VulkanVideoSink* QMLPlayer::videoSink() const {
    return impl_->videoSink.get();
}

int QMLPlayer::videoWidth() const {
    return impl_->videoWidth;
}

int QMLPlayer::videoHeight() const {
    return impl_->videoHeight;
}

double QMLPlayer::fps() const {
    return impl_->fpsValue;
}

double QMLPlayer::playbackRate() const {
    return impl_->playbackRateValue;
}

void QMLPlayer::setPlaybackRate(double rate) {
    if (qFuzzyCompare(impl_->playbackRateValue, rate)) return;
    impl_->playbackRateValue = rate;
    impl_->mediaPlayer->player()->setPlaybackRate(rate);
    emit playbackRateChanged();
}

bool QMLPlayer::lowLatencyMode() const {
    return impl_->lowLatencyModeValue;
}

void QMLPlayer::setLowLatencyMode(bool enabled) {
    if (impl_->lowLatencyModeValue == enabled) return;
    impl_->lowLatencyModeValue = enabled;
    impl_->mediaPlayer->player()->setLowLatency(enabled);
    emit lowLatencyModeChanged();
}

int QMLPlayer::reconnectAttempt() const {
    return impl_->reconnectAttemptValue;
}

QString QMLPlayer::connectionState() const {
    return impl_->connectionStateValue;
}

qreal QMLPlayer::streamBitrate() const {
    return impl_->streamBitrateValue;
}

int QMLPlayer::bufferDuration() const {
    return impl_->bufferDurationValue;
}

int QMLPlayer::droppedFrames() const {
    return impl_->droppedFramesValue;
}

bool QMLPlayer::subtitleVisible() const {
    return impl_->subtitleRenderer->isVisible();
}

void QMLPlayer::setSubtitleVisible(bool visible) {
    if (impl_->subtitleRenderer->isVisible() == visible) return;
    impl_->subtitleRenderer->setVisibility(visible);
    emit subtitleVisibleChanged();
}

bool QMLPlayer::hasSubtitles() const {
    return impl_->subtitleRenderer->hasSubtitles();
}

bool QMLPlayer::toggleSubtitles() {
    bool newVisible = impl_->subtitleRenderer->toggleVisibility();
    emit subtitleVisibleChanged();
    return newVisible;
}

bool QMLPlayer::loadSubtitleFile(const QString& path) {
    bool ok = impl_->subtitleRenderer->loadFile(path.toStdString());
    if (ok) {
        emit subtitleChanged();
        emit subtitleVisibleChanged();
    }
    return ok;
}

} // namespace qml
} // namespace hlplayer
