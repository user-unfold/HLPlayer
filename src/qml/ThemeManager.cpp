#include "ThemeManager.h"

#include <QGuiApplication>
#include <QStyleHints>

#include <spdlog/spdlog.h>

namespace hlplayer {
namespace qml {

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
    , accentColor_(0x8B, 0x5C, 0xF6) {
    applyTheme();
    spdlog::info("HLPlayer::ThemeManager constructed");
}

ThemeManager::~ThemeManager() {
    spdlog::info("HLPlayer::ThemeManager destructed");
}

ThemeManager::Theme ThemeManager::theme() const {
    return theme_;
}

void ThemeManager::setTheme(Theme theme) {
    if (theme_ == theme) return;
    theme_ = theme;
    applyTheme();
    emit themeChanged();
}

QColor ThemeManager::primary() const {
    return primary_;
}

QColor ThemeManager::surface() const {
    return surface_;
}

QColor ThemeManager::surfaceVariant() const {
    return surfaceVariant_;
}

QColor ThemeManager::onSurface() const {
    return onSurface_;
}

QColor ThemeManager::errorColor() const {
    return errorColor_;
}

QColor ThemeManager::accentColor() const {
    return accentColor_;
}

void ThemeManager::setAccentColor(const QColor& color) {
    if (accentColor_ == color) return;
    accentColor_ = color;
    emit themeChanged();
}

bool ThemeManager::isDarkMode() const {
    return isDarkMode_;
}

void ThemeManager::applyTheme() {
    bool dark = false;

    if (theme_ == Theme::System) {
        if (auto* app = QCoreApplication::instance()) {
            auto* guiApp = static_cast<QGuiApplication*>(app);
            dark = guiApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        }
    } else {
        dark = (theme_ == Theme::Dark);
    }

    isDarkMode_ = dark;

    if (dark) {
        primary_ = QColor(0x60, 0xCD, 0xFF);
        surface_ = QColor(0x20, 0x20, 0x20);
        surfaceVariant_ = QColor(0x2D, 0x2D, 0x2D);
        onSurface_ = QColor(0xFF, 0xFF, 0xFF);
        bgMain_ = QColor(0x0F, 0x0F, 0x0F);
        bgSidebar_ = QColor(0x1A, 0x1A, 0x1A);
        textPrimary_ = QColor(0xFF, 0xFF, 0xFF);
        textSecondary_ = QColor(0xAA, 0xAA, 0xAA);
        textDisabled_ = QColor(0x55, 0x55, 0x55);
        border_ = QColor(0x2A, 0x2A, 0x2A);
        hoverHighlight_ = QColor(0xFF, 0xFF, 0xFF, 20);
        controlBarBackstop_ = QColor(0x00, 0x00, 0x00, 230);
    } else {
        primary_ = QColor(0x00, 0x78, 0xD4);
        surface_ = QColor(0xF3, 0xF3, 0xF3);
        surfaceVariant_ = QColor(0xE5, 0xE5, 0xE5);
        onSurface_ = QColor(0x00, 0x00, 0x00);
        bgMain_ = QColor(0xFA, 0xFA, 0xFA);
        bgSidebar_ = QColor(0xF0, 0xF0, 0xF0);
        textPrimary_ = QColor(0x1A, 0x1A, 0x1A);
        textSecondary_ = QColor(0x66, 0x66, 0x66);
        textDisabled_ = QColor(0xAA, 0xAA, 0xAA);
        border_ = QColor(0xDD, 0xDD, 0xDD);
        hoverHighlight_ = QColor(0x00, 0x00, 0x00, 20);
        controlBarBackstop_ = QColor(0xFF, 0xFF, 0xFF, 230);
    }

    errorColor_ = QColor(0xFF, 0x00, 0x00);
    secondaryAccent_ = QColor(0xA7, 0x8B, 0xFA);
}

QColor ThemeManager::secondaryAccent() const { return secondaryAccent_; }

QColor ThemeManager::bgMain() const { return bgMain_; }
QColor ThemeManager::bgSidebar() const { return bgSidebar_; }
QColor ThemeManager::textPrimary() const { return textPrimary_; }
QColor ThemeManager::textSecondary() const { return textSecondary_; }
QColor ThemeManager::textDisabled() const { return textDisabled_; }
QColor ThemeManager::border() const { return border_; }
QColor ThemeManager::hoverHighlight() const { return hoverHighlight_; }
QColor ThemeManager::controlBarBackstop() const { return controlBarBackstop_; }

} // namespace qml
} // namespace hlplayer
