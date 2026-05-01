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
#include <QTimer>
#include <memory>

#include <VulkanVideoSink.h>
#include <SubtitleRenderer.h>

namespace hlplayer {

class EventBus;
namespace render {
    class VulkanVideoSink;
}

namespace qml {

class HLPLAYER_QML_API QMLPlayer : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int state READ state NOTIFY stateChanged)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(bool isPlaying READ isPlaying NOTIFY isPlayingChanged)
    Q_PROPERTY(bool isPaused READ isPaused NOTIFY isPausedChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoResolutionChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoResolutionChanged)
    Q_PROPERTY(double fps READ fps NOTIFY fpsChanged)
    Q_PROPERTY(double playbackRate READ playbackRate WRITE setPlaybackRate NOTIFY playbackRateChanged)
    Q_PROPERTY(bool lowLatencyMode READ lowLatencyMode WRITE setLowLatencyMode NOTIFY lowLatencyModeChanged)
    Q_PROPERTY(hlplayer::render::VulkanVideoSink* videoSink READ videoSink CONSTANT)
    Q_PROPERTY(int reconnectAttempt READ reconnectAttempt NOTIFY reconnectAttemptChanged)
    Q_PROPERTY(QString connectionState READ connectionState NOTIFY connectionStateChanged)
    Q_PROPERTY(qreal streamBitrate READ streamBitrate NOTIFY streamBitrateChanged)
    Q_PROPERTY(int bufferDuration READ bufferDuration NOTIFY bufferDurationChanged)
    Q_PROPERTY(int droppedFrames READ droppedFrames NOTIFY droppedFramesChanged)
    Q_PROPERTY(bool subtitleVisible READ subtitleVisible WRITE setSubtitleVisible NOTIFY subtitleVisibleChanged)
    Q_PROPERTY(bool hasSubtitles READ hasSubtitles NOTIFY subtitleChanged)

public:
    explicit QMLPlayer(QObject* parent = nullptr);
    ~QMLPlayer() override;

    QString source() const;
    void setSource(const QString& source);

    int state() const;

    double volume() const;
    void setVolume(double volume);

    double position() const;
    double duration() const;

    QString error() const;
    bool isPlaying() const;
    bool isPaused() const;
    
    int videoWidth() const;
    int videoHeight() const;
    double fps() const;
    
    double playbackRate() const;
    void setPlaybackRate(double rate);

    bool lowLatencyMode() const;
    void setLowLatencyMode(bool enabled);

    int reconnectAttempt() const;
    QString connectionState() const;

    qreal streamBitrate() const;
    int bufferDuration() const;
    int droppedFrames() const;

    hlplayer::render::VulkanVideoSink* videoSink() const;

    bool subtitleVisible() const;
    void setSubtitleVisible(bool visible);
    bool hasSubtitles() const;

    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE bool toggleSubtitles();
    Q_INVOKABLE bool loadSubtitleFile(const QString& path);

signals:
    void sourceChanged();
    void stateChanged();
    void volumeChanged();
    void positionChanged();
    void durationChanged();
    void errorChanged();
    void isPlayingChanged();
    void isPausedChanged();
    void videoResolutionChanged();
    void fpsChanged();
    void playbackRateChanged();
    void lowLatencyModeChanged();
    void reconnectAttemptChanged();
    void connectionStateChanged();
    void streamBitrateChanged();
    void bufferDurationChanged();
    void droppedFramesChanged();
    void subtitleVisibleChanged();
    void subtitleChanged();

private:
    void setupEventBusSubscription();
    void handleStateChangedEvent(int oldState, int newState);
    void handleErrorEvent(int errorCode, const QString& errorMessage);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qml
} // namespace hlplayer
