#include "PlayerI18nContext.h"

#include <QCoreApplication>
#include <QDir>

#include <memory>

#include <spdlog/spdlog.h>

namespace hlplayer {
namespace qml {

PlayerI18nContext::PlayerI18nContext(QObject* parent)
    : QObject(parent)
    , currentLanguage_("en") {
    spdlog::info("HLPlayer::PlayerI18nContext constructed, default language: en");
}

PlayerI18nContext::~PlayerI18nContext() {
    spdlog::info("HLPlayer::PlayerI18nContext destructed");
}

QString PlayerI18nContext::currentLanguage() const {
    return currentLanguage_;
}

void PlayerI18nContext::setLanguage(const QString& language) {
    if (currentLanguage_ == language) return;
    currentLanguage_ = language;

    auto newTranslator = std::make_unique<QTranslator>();
    bool loaded = false;
    QString filename = "hlplayer_" + language;

    if (QCoreApplication::instance()) {
        QString appDir = QCoreApplication::applicationDirPath();
        QStringList searchPaths = {
            appDir + "/../lang",
            appDir + "/lang",
            "lang"
        };

        for (const auto& dir : searchPaths) {
            if (newTranslator->load(filename, dir)) {
                loaded = true;
                break;
            }
        }
    } else {
        loaded = newTranslator->load(filename, "lang");
    }

    if (!loaded) {
        spdlog::warn("PlayerI18nContext: failed to load translation for '{}'",
                     language.toStdString());
    }

    if (loaded) {
        translator_ = std::move(newTranslator);
    } else {
        translator_.reset();
    }

    emit languageChanged();
}

QString PlayerI18nContext::tr(const QString& key) const {
    if (key.isEmpty()) return key;

    if (!translator_) return key;

    QByteArray utf8 = key.toUtf8();
    QString result = translator_->translate("HLPlayer", utf8.constData());
    if (result.isEmpty()) {
        return key;
    }
    return result;
}

} // namespace qml
} // namespace hlplayer
