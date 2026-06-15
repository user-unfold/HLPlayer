#pragma once
#include "CryptoExport.h"
#include "FileEncryptor.h"
#include <QObject>
#include <QString>
#include <memory>
#include <atomic>
#include <thread>

namespace hlplayer::crypto {

class HLPLAYER_CRYPTO_API EncryptionExporter : public QObject {
    Q_OBJECT

public:
    explicit EncryptionExporter(QObject* parent = nullptr);
    ~EncryptionExporter() override;

    // Check if a file is already encrypted (.hlv)
    Q_INVOKABLE bool isEncrypted(const QString& filePath) const;

    // Start encryption in a worker thread.
    // If usePassword=true: encrypt with password.
    // If usePassword=false: auto-generate key, emit keyString in finished signal.
    Q_INVOKABLE void startEncryption(const QString& inputPath,
                                      const QString& outputPath,
                                      bool usePassword,
                                      const QString& password);

    // Cancel ongoing encryption
    Q_INVOKABLE void cancel();

signals:
    void progressChanged(double percent);  // 0.0 to 1.0
    void encryptionFinished(const QString& keyString);  // empty if password mode
    void encryptionFailed(const QString& error);

private:
    std::atomic<bool> shouldCancel_{false};
    std::thread workerThread_;
    std::unique_ptr<FileEncryptor> encryptor_;

    void runEncryption(const std::string& inputPath,
                       const std::string& outputPath,
                       bool usePassword,
                       const std::string& password);
};

} // namespace hlplayer::crypto