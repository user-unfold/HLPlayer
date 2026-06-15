#include "AntiScreenshotManager.h"
#include <QQuickWindow>
#include <QGuiApplication>

#ifdef _WIN32
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x11
#endif
#endif

namespace hlplayer {
namespace qml {

AntiScreenshotManager::AntiScreenshotManager(QObject* parent)
    : QObject(parent)
    , m_settings("HLPlayer", "HLPlayer") {
    m_enabled = m_settings.value("antiScreenshotEnabled", false).toBool();
}

AntiScreenshotManager::~AntiScreenshotManager() {
    deactivate();
}

bool AntiScreenshotManager::enabled() const {
    return m_enabled;
}

void AntiScreenshotManager::setEnabled(bool enabled) {
    if (m_enabled == enabled) return;
    m_enabled = enabled;
    m_settings.setValue("antiScreenshotEnabled", enabled);
    emit enabledChanged();
}

void AntiScreenshotManager::activateForWindow(QObject* window) {
    applyAntiScreenshot(true, window);
}

void AntiScreenshotManager::deactivate() {
    QQuickWindow* window = qobject_cast<QQuickWindow*>(parent());
    if (!window) {
        window = qobject_cast<QQuickWindow*>(QGuiApplication::topLevelWindows().first());
    }
    if (!window) return;
    applyAntiScreenshot(false, window);
}

void AntiScreenshotManager::applyAntiScreenshot(bool enable, QObject* window) {
#ifdef _WIN32
    QQuickWindow* quickWindow = qobject_cast<QQuickWindow*>(window);
    if (!quickWindow) {
        quickWindow = qobject_cast<QQuickWindow*>(parent());
    }
    if (!quickWindow) {
        quickWindow = qobject_cast<QQuickWindow*>(QGuiApplication::topLevelWindows().first());
    }
    if (!quickWindow) return;

    HWND hwnd = reinterpret_cast<HWND>(quickWindow->winId());
    if (!hwnd) return;

    DWORD affinity = enable ? WDA_EXCLUDEFROMCAPTURE : 0;
    if (!SetWindowDisplayAffinity(hwnd, affinity)) {
        DWORD error = GetLastError();
        if (error == ERROR_INVALID_FUNCTION) {
            return;
        }
    }
#else
    Q_UNUSED(enable);
    Q_UNUSED(window);
#endif
}

} // namespace qml
} // namespace hlplayer