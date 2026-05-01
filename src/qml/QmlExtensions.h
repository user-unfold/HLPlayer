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

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API QmlExtensions : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit QmlExtensions(QObject* parent = nullptr);
    ~QmlExtensions() override;

    Q_INVOKABLE QString getDocumentsPath() const;
};

} // namespace qml
} // namespace hlplayer
