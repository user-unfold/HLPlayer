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

#include <hlplayer/StreamRecorder.h>

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API StreamRecorderBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(qreal fileSize READ fileSize NOTIFY fileSizeChanged)
    Q_PROPERTY(qreal duration READ duration NOTIFY durationChanged)

public:
    explicit StreamRecorderBridge(QObject* parent = nullptr);
    ~StreamRecorderBridge() override;

    bool recording() const;
    qreal fileSize() const;
    qreal duration() const;

    Q_INVOKABLE void start(const QString& outputDir);
    Q_INVOKABLE void stop();

signals:
    void recordingChanged();
    void fileSizeChanged();
    void durationChanged();

private:
    std::unique_ptr<StreamRecorder> recorder_;
    QTimer updateTimer_;
    void updateStats();
};

} // namespace qml
} // namespace hlplayer
