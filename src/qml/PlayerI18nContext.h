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
#include <QTranslator>
#include <QString>

#include <memory>

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API PlayerI18nContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString currentLanguage READ currentLanguage WRITE setLanguage NOTIFY languageChanged)

public:
    explicit PlayerI18nContext(QObject* parent = nullptr);
    ~PlayerI18nContext() override;

    QString currentLanguage() const;
    void setLanguage(const QString& language);

    Q_INVOKABLE QString tr(const QString& key) const;

signals:
    void languageChanged();

private:
    QString currentLanguage_;
    std::unique_ptr<QTranslator> translator_;
};

} // namespace qml
} // namespace hlplayer
