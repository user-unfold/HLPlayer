#pragma once

#ifndef HLPLAYER_QML_API
#ifdef _WIN32
    #ifdef HLPLAYER_QML_EXPORTS
        #define HLPLAYER_QML_API __declspec(dllexport)
    #else
        #define HLPLAYER_QML_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_QML_API
#endif
#endif

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QByteArray>
#include <memory>

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API OfflineVSRBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(int state READ state NOTIFY stateChanged)

    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(double currentFps READ currentFps NOTIFY fpsChanged)
    Q_PROPERTY(double estimatedTimeRemaining READ estimatedTimeRemaining NOTIFY etaChanged)
    Q_PROPERTY(QString currentStatus READ currentStatus NOTIFY statusChanged)

    Q_PROPERTY(qint64 vramUsedBytes READ vramUsedBytes NOTIFY vramChanged)
    Q_PROPERTY(qint64 vramTotalBytes READ vramTotalBytes NOTIFY vramChanged)

    Q_PROPERTY(int framesProcessed READ framesProcessed NOTIFY progressChanged)
    Q_PROPERTY(int totalFrames READ totalFrames NOTIFY progressChanged)

    Q_PROPERTY(QString sourcePath READ sourcePath NOTIFY sourceInfoChanged)
    Q_PROPERTY(int sourceWidth READ sourceWidth NOTIFY sourceInfoChanged)
    Q_PROPERTY(int sourceHeight READ sourceHeight NOTIFY sourceInfoChanged)
    Q_PROPERTY(double sourceDuration READ sourceDuration NOTIFY sourceInfoChanged)
    Q_PROPERTY(double sourceFps READ sourceFps NOTIFY sourceInfoChanged)

    Q_PROPERTY(QString outputPath READ outputPath WRITE setOutputPath NOTIFY outputPathChanged)
    Q_PROPERTY(int outputWidth READ outputWidth NOTIFY outputInfoChanged)
    Q_PROPERTY(int outputHeight READ outputHeight NOTIFY outputInfoChanged)

public:
    enum StudioState {
        Idle,
        Ready,
        Processing,
        Paused,
        Completed,
        Error
    };
    Q_ENUM(StudioState)

    explicit OfflineVSRBridge(QObject* parent = nullptr);
    ~OfflineVSRBridge() override;

    int state() const;

    double progress() const;
    double currentFps() const;
    double estimatedTimeRemaining() const;
    QString currentStatus() const;

    qint64 vramUsedBytes() const;
    qint64 vramTotalBytes() const;

    int framesProcessed() const;
    int totalFrames() const;

    QString sourcePath() const;
    int sourceWidth() const;
    int sourceHeight() const;
    double sourceDuration() const;
    double sourceFps() const;

    QString outputPath() const;
    void setOutputPath(const QString& path);
    int outputWidth() const;
    int outputHeight() const;

    Q_INVOKABLE void importVideo(const QString& path);
    Q_INVOKABLE void startProcessing();
    Q_INVOKABLE void pauseProcessing();
    Q_INVOKABLE void resumeProcessing();
    Q_INVOKABLE void cancelProcessing();
    Q_INVOKABLE void setScaleFactor(double factor);
    Q_INVOKABLE void setEncoder(const QString& encoder);
    Q_INVOKABLE void setQuality(int quality);
    Q_INVOKABLE void setPreset(const QString& preset);
    Q_INVOKABLE void setThreadCount(int count);
    Q_INVOKABLE void setPerformanceMode(const QString& mode);

signals:
    void stateChanged();
    void progressChanged();
    void fpsChanged();
    void etaChanged();
    void vramChanged();
    void statusChanged();
    void sourceInfoChanged();
    void outputPathChanged();
    void outputInfoChanged();
    void previewFrameReady(const QByteArray& frameData, int width, int height, bool isOriginal);
    void errorOccurred(const QString& errorMessage);
    void processingCompleted(const QString& outputPath);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qml
} // namespace hlplayer
