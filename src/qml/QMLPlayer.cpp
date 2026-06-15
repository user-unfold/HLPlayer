#include "QMLPlayer.h"
#include <hlplayer/FFPlayer.h>
#include <hlplayer/MediaPlayer.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/DirectStreamResolver.h>
#include <hlplayer/ASRPipeline.h>
#include <ExtractorFactory.h>
#include <VulkanVideoSink.h>
#include <SDLAudioRenderer.h>

#include <QtGlobal>
#include <QUrl>
#include <QDateTime>
#include <QEventLoop>
#include <QPromise>
#include <QFuture>

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
    QString source;
    double volume = 1.0;
    double duration = 0.0;
    double fpsValue = 0.0;
    double playbackRateValue = 1.0;
    double seekTarget_ = -1.0;
    qint64 seekStabilizeStart_ = 0;
    int videoWidth = 0;
    int videoHeight = 0;
    int stateChangeSubscriptionId = -1;
    int errorSubscriptionId = -1;
    int resolutionSubscriptionId = -1;
    QTimer* positionTimer = nullptr;
    QTimer* eventDispatchTimer = nullptr;
    QMLPlayer* owner = nullptr;
    std::string pendingPasswordInput;
    bool passwordPromptWaiting = false;
    std::promise<std::string> passwordPromise;
};

QMLPlayer::QMLPlayer(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {
    impl_->videoSink = std::make_unique<render::VulkanVideoSink>();

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
        emit positionChanged();
    });

    impl_->eventDispatchTimer = new QTimer(this);
    impl_->eventDispatchTimer->setInterval(30);
    connect(impl_->eventDispatchTimer, &QTimer::timeout, this, [this]() {
        impl_->mediaPlayer->eventBus().dispatch();
    });
    impl_->eventDispatchTimer->start();

    setupEventBusSubscription();

    ffPlayer->setPasswordCallback([this](const std::string& filePath, int keyMode) {
        return handlePasswordRequired(filePath, keyMode);
    });

    spdlog::info("HLPlayer::QMLPlayer constructed");
}


QMLPlayer::~QMLPlayer() {
    if (impl_->eventDispatchTimer) {
        impl_->eventDispatchTimer->stop();
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
    spdlog::info("HLPlayer::QMLPlayer destructed");
}

quintptr QMLPlayer::eventBusPointer() const {
    return reinterpret_cast<quintptr>(&impl_->mediaPlayer->eventBus());
}

void QMLPlayer::setAudioFrameCallback(const QVariant& pipelinePtr) {
    auto* ffPlayer = impl_->mediaPlayer->player();
    if (!ffPlayer) return;

    quintptr ptr = pipelinePtr.value<quintptr>();
    spdlog::info("QMLPlayer::setAudioFrameCallback called with ptr={}", ptr);
    if (ptr == 0) {
        ffPlayer->setAudioFrameCallback(nullptr);
        return;
    }

    auto* pipeline = reinterpret_cast<asr::ASRPipeline*>(ptr);
    ffPlayer->setAudioFrameCallback([pipeline](const AudioFrame& frame) {
        pipeline->feedAudioFrame(frame);
    });
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

    if (newState == static_cast<int>(PlayerState_Error)) {
        impl_->positionTimer->stop();
    }

    if (newState == static_cast<int>(PlayerState_End)) {
        // Keep the position timer running so the progress bar smoothly
        // reaches duration while the refresh thread drains remaining frames.
        // The timer is stopped when the player returns to Idle (e.g. next file).
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
    if (!source.isEmpty()) {
        QUrl url(source);
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

    impl_->mediaPlayer->open(normalizedSource.toStdString());

    QMetaObject::invokeMethod(this, [this]() {
        emit sourceChanged();
        emit positionChanged();
        emit durationChanged();
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
}

void QMLPlayer::stop() {
    impl_->seekTarget_ = -1.0;
    impl_->seekStabilizeStart_ = 0;
    impl_->mediaPlayer->stop();
    impl_->positionTimer->stop();
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

QString QMLPlayer::promptForPassword(const QString& filePath, int keyMode) {
    QString result;
    impl_->pendingPasswordInput.clear();

    if (impl_->passwordPromptWaiting) {
        return QString();
    }

    impl_->passwordPromptWaiting = true;
    impl_->passwordPromise = std::promise<std::string>();

    emit passwordPromptRequested(filePath, keyMode);

    QEventLoop loop;
    QTimer::singleShot(300000, &loop, &QEventLoop::quit);

    auto future = impl_->passwordPromise.get_future();
    while (future.wait_for(std::chrono::milliseconds(10)) != std::future_status::ready) {
        loop.processEvents();
    }

    if (future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        result = QString::fromStdString(future.get());
    }

    impl_->passwordPromptWaiting = false;
    return result;
}

std::string QMLPlayer::handlePasswordRequired(const std::string& filePath, int keyMode) {
    if (QThread::currentThread() == this->thread()) {
        QString qFilePath = QString::fromStdString(filePath);
        QString result = promptForPassword(qFilePath, keyMode);
        return result.toStdString();
    } else {
        QString result;
        QMetaObject::invokeMethod(
            this,
            [this, filePath, keyMode, &result]() {
                result = promptForPassword(QString::fromStdString(filePath), keyMode);
            },
            Qt::BlockingQueuedConnection
        );
        return result.toStdString();
    }
}

void QMLPlayer::setPasswordInput(const QString& input) {
    if (impl_->passwordPromptWaiting) {
        impl_->pendingPasswordInput = input.toStdString();
        try {
            impl_->passwordPromise.set_value(impl_->pendingPasswordInput);
        } catch (const std::future_error&) {
        }
    }
}

} // namespace qml
} // namespace hlplayer
