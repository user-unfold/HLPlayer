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
#include <QColor>

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API ThemeManager : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Theme theme READ theme WRITE setTheme NOTIFY themeChanged)
    Q_PROPERTY(QColor primary READ primary NOTIFY themeChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY themeChanged)
    Q_PROPERTY(QColor surfaceVariant READ surfaceVariant NOTIFY themeChanged)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY themeChanged)
    Q_PROPERTY(QColor errorColor READ errorColor NOTIFY themeChanged)
    Q_PROPERTY(QColor accentColor READ accentColor WRITE setAccentColor NOTIFY themeChanged)
    Q_PROPERTY(bool isDarkMode READ isDarkMode NOTIFY themeChanged)

public:
    enum class Theme {
        Light = 0,
        Dark = 1,
        System = 2
    };
    Q_ENUM(Theme)

    explicit ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    Theme theme() const;
    void setTheme(Theme theme);

    QColor primary() const;
    QColor surface() const;
    QColor surfaceVariant() const;
    QColor onSurface() const;
    QColor errorColor() const;

    QColor accentColor() const;
    void setAccentColor(const QColor& color);

    bool isDarkMode() const;

signals:
    void themeChanged();

private:
    void applyTheme();

    Theme theme_ = Theme::System;
    QColor accentColor_;
    QColor primary_;
    QColor surface_;
    QColor surfaceVariant_;
    QColor onSurface_;
    QColor errorColor_;
    bool isDarkMode_ = false;
};

} // namespace qml
} // namespace hlplayer
