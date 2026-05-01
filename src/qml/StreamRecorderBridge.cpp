#include "StreamRecorderBridge.h"

#include <QDebug>

namespace hlplayer {
namespace qml {

StreamRecorderBridge::StreamRecorderBridge(QObject* parent)
    : QObject(parent)
    , recorder_(std::make_unique<StreamRecorder>())
{
    updateTimer_.setInterval(1000);
    connect(&updateTimer_, &QTimer::timeout, this, &StreamRecorderBridge::updateStats);
}

StreamRecorderBridge::~StreamRecorderBridge() {
    if (recorder_->isRecording()) {
        recorder_->stop();
    }
}

bool StreamRecorderBridge::recording() const {
    return recorder_->isRecording();
}

qreal StreamRecorderBridge::fileSize() const {
    return static_cast<qreal>(recorder_->currentFileSize());
}

qreal StreamRecorderBridge::duration() const {
    return static_cast<qreal>(recorder_->currentDuration());
}

void StreamRecorderBridge::start(const QString& outputDir) {
    if (recorder_->isRecording()) {
        qDebug() << "StreamRecorder: Already recording";
        return;
    }

    auto result = recorder_->start(outputDir.toStdString());
    if (result.hasError()) {
        qDebug() << "StreamRecorder: Failed to start, error code:" << static_cast<int>(result.error());
        return;
    }

    updateTimer_.start();
    emit recordingChanged();
    emit fileSizeChanged();
    emit durationChanged();
}

void StreamRecorderBridge::stop() {
    if (!recorder_->isRecording()) {
        return;
    }

    recorder_->stop();
    updateTimer_.stop();
    emit recordingChanged();
}

void StreamRecorderBridge::updateStats() {
    if (!recorder_->isRecording()) {
        return;
    }
    emit fileSizeChanged();
    emit durationChanged();
}

} // namespace qml
} // namespace hlplayer
