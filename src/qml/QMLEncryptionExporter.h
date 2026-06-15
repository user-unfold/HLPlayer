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
#include <memory>

namespace hlplayer {
namespace crypto {
class EncryptionExporter;
}

namespace qml {

class HLPLAYER_QML_API QMLEncryptionExporter : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(bool isProcessing READ isProcessing NOTIFY stateChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY errorOccurred)

public:
    explicit QMLEncryptionExporter(QObject* parent = nullptr);
    ~QMLEncryptionExporter() override;

    bool isProcessing() const;
    double progress() const;
    QString lastError() const;

    Q_INVOKABLE bool isEncrypted(const QString& filePath) const;
    Q_INVOKABLE void startEncryption(const QString& inputPath,
                                      const QString& outputPath,
                                      bool usePassword,
                                      const QString& password);
    Q_INVOKABLE void cancel();

signals:
    void stateChanged();
    void progressChanged();
    void encryptionFinished(const QString& keyString);
    void errorOccurred(const QString& error);

private slots:
    void handleProgressChanged(double percent);
    void handleEncryptionFinished(const QString& keyString);
    void handleEncryptionFailed(const QString& error);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace qml
} // namespace hlplayer