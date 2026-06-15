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
#include <QSettings>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API AntiScreenshotManager : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)

public:
    explicit AntiScreenshotManager(QObject* parent = nullptr);
    ~AntiScreenshotManager() override;

    Q_INVOKABLE bool enabled() const;
    Q_INVOKABLE void setEnabled(bool enabled);

    Q_INVOKABLE void activateForWindow(QObject* window);
    Q_INVOKABLE void deactivate();

signals:
    void enabledChanged();

private:
    void applyAntiScreenshot(bool enable, QObject* window);
    bool m_enabled;
    QSettings m_settings;
};

} // namespace qml
} // namespace hlplayer