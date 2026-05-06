#pragma once

#ifndef HLPLAYER_CAMERA_QML_API
#ifdef _WIN32
    #ifdef HLPLAYER_CAMERA_QML_EXPORTS
        #define HLPLAYER_CAMERA_QML_API __declspec(dllexport)
    #else
        #define HLPLAYER_CAMERA_QML_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_CAMERA_QML_API
#endif
#endif

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QTimer>
#include <QQuickImageProvider>
#include <memory>

#include <hlplayer/CameraTypes.h>

namespace hlplayer {

namespace camera {
class RecordingPipeline;
class StreamingPipeline;
class PreviewRenderer;
}

namespace qml {

class HLPLAYER_CAMERA_QML_API QMLCameraRecorder : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isRecording READ isRecording NOTIFY isRecordingChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY isPausedChanged)
    Q_PROPERTY(double recordingProgress READ recordingProgress NOTIFY recordingProgressChanged)
    Q_PROPERTY(double recordingDuration READ recordingDuration NOTIFY recordingDurationChanged)
    Q_PROPERTY(QString recordingFilePath READ recordingFilePath NOTIFY recordingFilePathChanged)
    Q_PROPERTY(double currentFps READ currentFps NOTIFY currentFpsChanged)
    Q_PROPERTY(bool isPreviewEnabled READ isPreviewEnabled WRITE setIsPreviewEnabled NOTIFY isPreviewEnabledChanged)
    Q_PROPERTY(QQuickImageProvider* previewImageProvider READ previewImageProvider CONSTANT)

    Q_PROPERTY(QStringList cameraList READ cameraList NOTIFY cameraListChanged)
    Q_PROPERTY(QStringList micList READ micList NOTIFY micListChanged)
    Q_PROPERTY(QString selectedCamera READ selectedCamera WRITE setSelectedCamera NOTIFY selectedCameraChanged)
    Q_PROPERTY(QString selectedMic READ selectedMic WRITE setSelectedMic NOTIFY selectedMicChanged)
    Q_PROPERTY(QString fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(QString fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(int previewRefreshCounter READ previewRefreshCounter NOTIFY previewRefreshCounterChanged)

    Q_PROPERTY(bool isStreaming READ isStreaming NOTIFY isStreamingChanged)
    Q_PROPERTY(double streamingProgress READ streamingProgress NOTIFY streamingProgressChanged)
    Q_PROPERTY(QString streamingState READ streamingState NOTIFY streamingStateChanged)
    Q_PROPERTY(QString streamingBitrate READ streamingBitrate NOTIFY streamingBitrateChanged)

public:
    explicit QMLCameraRecorder(QObject* parent = nullptr);
    ~QMLCameraRecorder() override;

    bool isRecording() const;
    bool isPaused() const;
    double recordingProgress() const;
    double recordingDuration() const;
    QString recordingFilePath() const;
    double currentFps() const;
    bool isPreviewEnabled() const;
    void setIsPreviewEnabled(bool enabled);
    QQuickImageProvider* previewImageProvider() const;

    QStringList cameraList() const;
    QStringList micList() const;
    QString selectedCamera() const;
    void setSelectedCamera(const QString& camera);
    QString selectedMic() const;
    void setSelectedMic(const QString& mic);
    QString fileSize() const;
    QString fps() const;
    int previewRefreshCounter() const;

    bool isStreaming() const;
    double streamingProgress() const;
    QString streamingState() const;
    QString streamingBitrate() const;

    Q_INVOKABLE void startRecording(const QString& outputPath, int width, int height, int frameRate, int videoBitrate, const QString& cameraDevicePath, const QString& micDevicePath);
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    Q_INVOKABLE void toggleRecording();

    Q_INVOKABLE void enumerateCameras();
    Q_INVOKABLE void enumerateMics();

    Q_INVOKABLE void startStreaming(const QString& url, const QString& sourcePath);
    Q_INVOKABLE void cancelStreaming();

signals:
    void isRecordingChanged();
    void isPausedChanged();
    void recordingProgressChanged();
    void recordingDurationChanged();
    void recordingFilePathChanged();
    void currentFpsChanged();
    void isPreviewEnabledChanged();

    void cameraListChanged();
    void micListChanged();
    void selectedCameraChanged();
    void selectedMicChanged();
    void fileSizeChanged();
    void fpsChanged();
    void previewRefreshCounterChanged();

    void isStreamingChanged();
    void streamingProgressChanged();
    void streamingStateChanged();
    void streamingBitrateChanged();

private:
    void setupRecordingCallback();
    void setupStreamingCallback();
    void handleRecordingStateChange(hlplayer::RecordingState newState);
    void handleStreamingStateChange(hlplayer::StreamingState newState);
    void updateRecordingStats();
    void updateStreamingStats();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
}
