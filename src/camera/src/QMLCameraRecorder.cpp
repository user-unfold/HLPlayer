#include <hlplayer/QMLCameraRecorder.h>
#include <hlplayer/RecordingPipelineV2.h>
#include <hlplayer/StreamingPipeline.h>
#include <hlplayer/PreviewRenderer.h>
#include <hlplayer/AudioEncoder.h>
#include <hlplayer/CameraSource.h>

#include <QtGlobal>
#include <QUrl>
#include <QFileInfo>

#include <spdlog/spdlog.h>

#include <atomic>
#include <memory>
#include <string>

namespace hlplayer {
namespace qml {

struct QMLCameraRecorder::Impl {
    std::unique_ptr<RecordingPipelineV2> recordingPipeline;
    std::unique_ptr<StreamingPipeline> streamingPipeline;
    std::unique_ptr<PreviewRenderer> previewRenderer;

    bool isRecordingValue = false;
    bool isPausedValue = false;
    double recordingProgressValue = 0.0;
    double recordingDurationValue = 0.0;
    QString recordingFilePathValue;
    double currentFpsValue = 0.0;
    bool isPreviewEnabledValue = false;

    bool isStreamingValue = false;
    double streamingProgressValue = 0.0;
    QString streamingStateValue = "Idle";
    QString streamingBitrateValue = "0 kbps";

    QTimer* statsTimer = nullptr;
    QMLCameraRecorder* owner = nullptr;

    int previewRefreshCounter = 0;

    RecordingStats recordingStats;
    StreamingStats streamingStats;

    QStringList cameraListValue;
    QStringList micListValue;
    QStringList cameraPathListValue;
    QStringList micPathListValue;
    QString selectedCameraValue;
    QString selectedMicValue;
};

QMLCameraRecorder::QMLCameraRecorder(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {

    impl_->recordingPipeline = std::make_unique<RecordingPipelineV2>();
    impl_->streamingPipeline = std::make_unique<StreamingPipeline>();
    impl_->previewRenderer = std::make_unique<PreviewRenderer>();

    impl_->owner = this;

    impl_->statsTimer = new QTimer(this);
    impl_->statsTimer->setInterval(100);
    connect(impl_->statsTimer, &QTimer::timeout, this, [this]() {
        updateRecordingStats();
        updateStreamingStats();
    });

    setupRecordingCallback();
    setupStreamingCallback();

    spdlog::info("HLPlayer::QMLCameraRecorder constructed");
}

QMLCameraRecorder::~QMLCameraRecorder() {
    if (impl_->isRecordingValue) {
        impl_->recordingPipeline->stop();
    }
    if (impl_->isStreamingValue) {
        impl_->streamingPipeline->cancel();
    }
    if (impl_->statsTimer) {
        impl_->statsTimer->stop();
    }
    spdlog::info("HLPlayer::QMLCameraRecorder destructed");
}

void QMLCameraRecorder::setupRecordingCallback() {
}

void QMLCameraRecorder::setupStreamingCallback() {
}

void QMLCameraRecorder::handleRecordingStateChange(RecordingState newState) {
    spdlog::info("QMLCameraRecorder: Recording state changed to {}",
                 static_cast<int>(newState));

    bool wasRecording = impl_->isRecordingValue;
    bool wasPaused = impl_->isPausedValue;

    impl_->isRecordingValue = (newState == RecordingState::Recording || newState == RecordingState::Paused);
    impl_->isPausedValue = (newState == RecordingState::Paused);

    if (impl_->isRecordingValue != wasRecording)
        emit isRecordingChanged();
    if (impl_->isPausedValue != wasPaused)
        emit isPausedChanged();

    if (newState == RecordingState::Recording || newState == RecordingState::Paused) {
        impl_->statsTimer->start();
    } else {
        impl_->statsTimer->stop();
    }
}

void QMLCameraRecorder::handleStreamingStateChange(StreamingState newState) {
    spdlog::info("QMLCameraRecorder: Streaming state changed to {}",
                 static_cast<int>(newState));

    impl_->isStreamingValue = (newState == StreamingState::Streaming);

    QString newStateStr;
    switch (newState) {
        case StreamingState::Idle:
            newStateStr = "Idle";
            break;
        case StreamingState::Connecting:
            newStateStr = "Connecting";
            break;
        case StreamingState::Streaming:
            newStateStr = "Streaming";
            break;
        case StreamingState::Completed:
            newStateStr = "Completed";
            break;
        case StreamingState::Failed:
            newStateStr = "Failed";
            break;
        case StreamingState::Cancelled:
            newStateStr = "Cancelled";
            break;
    }

    if (impl_->streamingStateValue != newStateStr) {
        impl_->streamingStateValue = newStateStr;
        emit streamingStateChanged();
    }

    emit isStreamingChanged();

    if (newState == StreamingState::Streaming ||
        newState == StreamingState::Connecting) {
        impl_->statsTimer->start();
    } else {
        impl_->statsTimer->stop();
    }
}

void QMLCameraRecorder::updateRecordingStats() {
    auto stats = impl_->recordingPipeline->getStats();
    impl_->recordingStats = stats;

    if (!qFuzzyCompare(impl_->recordingDurationValue, stats.durationSeconds)) {
        impl_->recordingDurationValue = stats.durationSeconds;
        emit recordingDurationChanged();
    }

    if (!qFuzzyCompare(impl_->currentFpsValue, stats.currentFps)) {
        impl_->currentFpsValue = stats.currentFps;
        emit currentFpsChanged();
        emit fpsChanged();
    }

    emit fileSizeChanged();

    impl_->previewRefreshCounter++;
    emit previewRefreshCounterChanged();
}

void QMLCameraRecorder::updateStreamingStats() {
    auto stats = impl_->streamingPipeline->getStats();

    if (!qFuzzyCompare(impl_->streamingProgressValue, stats.progress)) {
        impl_->streamingProgressValue = stats.progress;
        emit streamingProgressChanged();
    }

    QString bitrateStr = QString("%1 kbps").arg(static_cast<int>(stats.currentBitrate / 1000.0));
    if (impl_->streamingBitrateValue != bitrateStr) {
        impl_->streamingBitrateValue = bitrateStr;
        emit streamingBitrateChanged();
    }
}

bool QMLCameraRecorder::isRecording() const {
    return impl_->isRecordingValue;
}

bool QMLCameraRecorder::isPaused() const {
    return impl_->isPausedValue;
}

double QMLCameraRecorder::recordingProgress() const {
    return impl_->recordingProgressValue;
}

double QMLCameraRecorder::recordingDuration() const {
    return impl_->recordingDurationValue;
}

QString QMLCameraRecorder::recordingFilePath() const {
    return impl_->recordingFilePathValue;
}

double QMLCameraRecorder::currentFps() const {
    return impl_->currentFpsValue;
}

bool QMLCameraRecorder::isPreviewEnabled() const {
    return impl_->isPreviewEnabledValue;
}

void QMLCameraRecorder::setIsPreviewEnabled(bool enabled) {
    if (impl_->isPreviewEnabledValue == enabled) return;
    impl_->isPreviewEnabledValue = enabled;
    emit isPreviewEnabledChanged();
}

QQuickImageProvider* QMLCameraRecorder::previewImageProvider() const {
    return impl_->previewRenderer->getImageProvider();
}

bool QMLCameraRecorder::isStreaming() const {
    return impl_->isStreamingValue;
}

double QMLCameraRecorder::streamingProgress() const {
    return impl_->streamingProgressValue;
}

QString QMLCameraRecorder::streamingState() const {
    return impl_->streamingStateValue;
}

QString QMLCameraRecorder::streamingBitrate() const {
    return impl_->streamingBitrateValue;
}

QStringList QMLCameraRecorder::cameraList() const {
    return impl_->cameraListValue;
}

QStringList QMLCameraRecorder::micList() const {
    return impl_->micListValue;
}

QString QMLCameraRecorder::selectedCamera() const {
    return impl_->selectedCameraValue;
}

void QMLCameraRecorder::setSelectedCamera(const QString& camera) {
    if (impl_->selectedCameraValue == camera) return;
    impl_->selectedCameraValue = camera;
    emit selectedCameraChanged();
}

QString QMLCameraRecorder::selectedMic() const {
    return impl_->selectedMicValue;
}

void QMLCameraRecorder::setSelectedMic(const QString& mic) {
    if (impl_->selectedMicValue == mic) return;
    impl_->selectedMicValue = mic;
    emit selectedMicChanged();
}

QString QMLCameraRecorder::fileSize() const {
    int64_t bytes = impl_->recordingStats.fileSizeBytes;
    double mb = static_cast<double>(bytes) / (1024.0 * 1024.0);
    return QString("%1 MB").arg(mb, 0, 'f', 1);
}

QString QMLCameraRecorder::fps() const {
    return QString("%1").arg(impl_->recordingStats.currentFps, 0, 'f', 1);
}

int QMLCameraRecorder::previewRefreshCounter() const {
    return impl_->previewRefreshCounter;
}

void QMLCameraRecorder::enumerateCameras() {
    CameraSource source;
    auto result = source.enumerateDevices();
    impl_->cameraListValue.clear();
    impl_->cameraPathListValue.clear();
    if (!result.hasError()) {
        for (const auto& dev : result.value()) {
            impl_->cameraListValue.append(QString::fromStdString(dev.name));
            impl_->cameraPathListValue.append(QString::fromStdString(dev.devicePath));
        }
    }
    emit cameraListChanged();
}

void QMLCameraRecorder::enumerateMics() {
    auto result = AudioCapture::enumerateAudioDevices();
    impl_->micListValue.clear();
    impl_->micPathListValue.clear();
    if (!result.hasError()) {
        for (const auto& dev : result.value()) {
            impl_->micListValue.append(QString::fromStdString(dev.name));
            impl_->micPathListValue.append(QString::fromStdString(dev.devicePath));
        }
    }
    emit micListChanged();
}

void QMLCameraRecorder::startRecording(const QString& outputPath, int width, int height, int frameRate, int videoBitrate, const QString& cameraDevicePath, const QString& micDevicePath) {
    spdlog::info("QMLCameraRecorder::startRecording camera='{}' mic='{}' {}x{}@{}",
                 cameraDevicePath.toStdString(), micDevicePath.toStdString(), width, height, frameRate);

    if (impl_->isRecordingValue) {
        spdlog::warn("QMLCameraRecorder::startRecording: Already recording");
        return;
    }

    impl_->recordingFilePathValue = outputPath;
    emit recordingFilePathChanged();

    impl_->recordingPipeline->setPreviewRenderer(impl_->previewRenderer.get());

    impl_->previewRenderer->init(width, height);

    impl_->statsTimer->start();

    QString cameraPath = cameraDevicePath;
    int camIdx = impl_->cameraListValue.indexOf(cameraDevicePath);
    if (camIdx >= 0 && camIdx < impl_->cameraPathListValue.size()) {
        cameraPath = impl_->cameraPathListValue[camIdx];
    }

    QString micPath = micDevicePath;
    int micIdx = impl_->micListValue.indexOf(micDevicePath);
    if (micIdx >= 0 && micIdx < impl_->micPathListValue.size()) {
        micPath = impl_->micPathListValue[micIdx];
    }

    spdlog::info("QMLCameraRecorder::startRecording resolved camera='{}' mic='{}'",
                 cameraPath.toStdString(), micPath.toStdString());

    RecordingConfig config;
    config.outputPath = outputPath.toStdString();
    config.resolution = {"custom", width, height, frameRate};
    config.videoBitrate = videoBitrate;
    config.cameraDevicePath = cameraPath.toStdString();
    config.micDevicePath = micPath.toStdString();

    auto result = impl_->recordingPipeline->start(config,
        [this](RecordingState state, const RecordingStats& stats) {
            // Use AutoConnection so the callback fires synchronously when on the
            // same thread (recording states always change from the main thread).
            // QueuedConnection can silently fail if Qt's meta-object system
            // cannot copy the functor across threads, causing isRecording to
            // never become true and the QML preview to stay black.
            bool ok = QMetaObject::invokeMethod(
                this,
                [this, state, stats]() {
                    impl_->recordingStats = stats;
                    handleRecordingStateChange(state);
                },
                Qt::AutoConnection
            );
            if (!ok) {
                // Fallback: call directly when invokeMethod fails (e.g. Qt
                // cannot queue the functor). This can happen on some Qt
                // builds where std::function-based invokeMethod is flaky.
                spdlog::warn("QMLCameraRecorder: invokeMethod failed, calling handler directly");
                impl_->recordingStats = stats;
                handleRecordingStateChange(state);
            }
        }
    );

    if (result.hasError()) {
        spdlog::error("QMLCameraRecorder::startRecording failed: {}",
                      static_cast<int>(result.error()));
        impl_->recordingFilePathValue.clear();
        emit recordingFilePathChanged();
    }
}

void QMLCameraRecorder::startRecordingWithStream(
    const QString& outputPath, const QString& streamUrl, int outputMode,
    int width, int height, int frameRate, int videoBitrate,
    const QString& cameraDevicePath, const QString& micDevicePath)
{
    if (impl_->isRecordingValue) {
        spdlog::warn("QMLCameraRecorder::startRecordingWithStream: Already recording");
        return;
    }

    impl_->recordingFilePathValue = outputPath;
    emit recordingFilePathChanged();

    impl_->recordingPipeline->setPreviewRenderer(impl_->previewRenderer.get());
    impl_->previewRenderer->init(width, height);
    impl_->statsTimer->start();

    QString cameraPath = cameraDevicePath;
    int camIdx = impl_->cameraListValue.indexOf(cameraDevicePath);
    if (camIdx >= 0 && camIdx < impl_->cameraPathListValue.size())
        cameraPath = impl_->cameraPathListValue[camIdx];

    QString micPath = micDevicePath;
    int micIdx = impl_->micListValue.indexOf(micDevicePath);
    if (micIdx >= 0 && micIdx < impl_->micPathListValue.size())
        micPath = impl_->micPathListValue[micIdx];

    spdlog::info("QMLCameraRecorder::startRecordingWithStream camera='{}' mic='{}' stream='{}' mode={}",
                 cameraPath.toStdString(), micPath.toStdString(),
                 streamUrl.toStdString(), outputMode);

    RecordingConfig config;
    config.outputPath = outputPath.toStdString();
    config.streamUrl = streamUrl.toStdString();
    config.outputMode = static_cast<RecordingConfig::OutputMode>(outputMode);
    config.resolution = {"custom", width, height, frameRate};
    config.videoBitrate = videoBitrate;
    config.cameraDevicePath = cameraPath.toStdString();
    config.micDevicePath = micPath.toStdString();

    auto result = impl_->recordingPipeline->start(config,
        [this](RecordingState state, const RecordingStats& stats) {
            bool ok = QMetaObject::invokeMethod(this,
                [this, state, stats]() {
                    impl_->recordingStats = stats;
                    handleRecordingStateChange(state);
                }, Qt::AutoConnection);
            if (!ok) {
                spdlog::warn("QMLCameraRecorder: invokeMethod failed, calling handler directly");
                impl_->recordingStats = stats;
                handleRecordingStateChange(state);
            }
        }
    );

    if (result.hasError()) {
        spdlog::error("QMLCameraRecorder::startRecordingWithStream failed: {}",
                      static_cast<int>(result.error()));
        impl_->recordingFilePathValue.clear();
        emit recordingFilePathChanged();
    }
}

void QMLCameraRecorder::stopRecording() {
    if (!impl_->isRecordingValue) {
        spdlog::warn("QMLCameraRecorder::stopRecording: Not recording");
        return;
    }

    impl_->statsTimer->stop();
    impl_->recordingPipeline->stop();
}

void QMLCameraRecorder::pauseRecording() {
    if (!impl_->isRecordingValue || impl_->isPausedValue) {
        spdlog::warn("QMLCameraRecorder::pauseRecording: Not in recording state");
        return;
    }

    impl_->recordingPipeline->pause();
}

void QMLCameraRecorder::resumeRecording() {
    if (!impl_->isPausedValue) {
        spdlog::warn("QMLCameraRecorder::resumeRecording: Not paused");
        return;
    }

    impl_->recordingPipeline->resume();
}

void QMLCameraRecorder::toggleRecording() {
    if (impl_->isRecordingValue) {
        stopRecording();
    } else {
        spdlog::warn("QMLCameraRecorder::toggleRecording: No recording config, use startRecording()");
    }
}

void QMLCameraRecorder::startStreaming(const QString& url, const QString& sourcePath) {
    if (impl_->isStreamingValue) {
        spdlog::warn("QMLCameraRecorder::startStreaming: Already streaming");
        return;
    }

    StreamingConfig config;
    config.url = url.toStdString();
    config.sourcePath = sourcePath.toStdString();
    config.videoBitrate = 4000000;
    config.keyframeInterval = 2;
    config.audioBitrate = 128000;

    auto result = impl_->streamingPipeline->start(config,
        [this](StreamingState state, const StreamingStats& stats) {
            QMetaObject::invokeMethod(
                this,
                [this, state, stats]() {
                    impl_->streamingStats = stats;
                    handleStreamingStateChange(state);
                },
                Qt::QueuedConnection
            );
        }
    );

    if (result.hasError()) {
        spdlog::error("QMLCameraRecorder::startStreaming failed: {}",
                      static_cast<int>(result.error()));
    }
}

void QMLCameraRecorder::cancelStreaming() {
    if (!impl_->isStreamingValue) {
        spdlog::warn("QMLCameraRecorder::cancelStreaming: Not streaming");
        return;
    }

    impl_->streamingPipeline->cancel();
}

}
}
