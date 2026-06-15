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

    Q_PROPERTY(bool protectionEnabled READ protectionEnabled WRITE setProtectionEnabled NOTIFY protectionEnabledChanged)

public:
    explicit AntiScreenshotManager(QObject* parent = nullptr);
    ~AntiScreenshotManager() override;

    Q_INVOKABLE bool protectionEnabled() const;
    Q_INVOKABLE void setProtectionEnabled(bool enabled);

    Q_INVOKABLE void activateForWindow(QObject* window);
    Q_INVOKABLE void deactivate();

signals:
    void protectionEnabledChanged();

private:
    void applyAntiScreenshot(bool enable, QObject* window);
    bool m_protectionEnabled;
    QSettings m_settings;
};

} // namespace qml
} // namespace hlplayer