#include "EncryptionExporter.h"
#include "FileEncryptor.h"
#include "KeyManager.h"
#include "HlvHeader.h"

namespace hlplayer::crypto {

EncryptionExporter::EncryptionExporter(QObject* parent)
    : QObject(parent)
    , encryptor_(std::make_unique<FileEncryptor>()) {}

EncryptionExporter::~EncryptionExporter() {
    cancel();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

bool EncryptionExporter::isEncrypted(const QString& filePath) const {
    return hasHlvExtension(filePath.toStdString());
}

void EncryptionExporter::startEncryption(const QString& inputPath,
                                          const QString& outputPath,
                                          bool usePassword,
                                          const QString& password) {
    // Join previous worker thread if it finished (should be done since signal was emitted)
    if (workerThread_.joinable()) {
        workerThread_.join();
    }

    shouldCancel_.store(false);

    std::string input = inputPath.toStdString();
    std::string output = outputPath.toStdString();
    bool usePass = usePassword;
    std::string pass = password.toStdString();

    workerThread_ = std::thread([this, input, output, usePass, pass]() {
        runEncryption(input, output, usePass, pass);
    });
}

void EncryptionExporter::cancel() {
    shouldCancel_.store(true);
    if (encryptor_) {
        encryptor_->cancel();
    }
}

void EncryptionExporter::runEncryption(const std::string& inputPath,
                                        const std::string& outputPath,
                                        bool usePassword,
                                        const std::string& password) {
    // Prevent re-encryption
    if (hasHlvExtension(inputPath)) {
        emit encryptionFailed("Cannot re-encrypt an already encrypted file");
        return;
    }

    // Extract original extension from input path
    size_t dotPos = inputPath.find_last_of('.');
    std::string ext = (dotPos != std::string::npos)
        ? inputPath.substr(dotPos + 1) : "mp4";
    if (ext.length() > 7) ext = ext.substr(0, 7);

    EncryptConfig config;
    config.inputPath = inputPath;
    config.outputPath = outputPath;
    config.originalExt = ext;

    if (usePassword) {
        config.keyMode = KeyMode::Password;
        config.password = password;
    } else {
        config.keyMode = KeyMode::RawKey;
        // rawKey left empty — FileEncryptor will generate one
    }

    encryptor_ = std::make_unique<FileEncryptor>();

    EncryptResult result = encryptor_->encrypt(config, [this](double progress) {
        emit progressChanged(progress);
    });

    if (result.success) {
        QString keyStr;
        if (!usePassword && result.generatedKey.size() == 32) {
            keyStr = QString::fromStdString(
                KeyManager::formatKeyString(result.generatedKey));
        }
        emit encryptionFinished(keyStr);
    } else {
        // Map error codes to user-friendly messages
        QString errMsg;
        switch (result.error) {
            case EncryptError::AlreadyEncrypted:
                errMsg = "File is already encrypted"; break;
            case EncryptError::InputNotFound:
                errMsg = "Input file not found"; break;
            case EncryptError::DiskFull:
                errMsg = "Insufficient disk space"; break;
            case EncryptError::Cancelled:
                errMsg = "Encryption cancelled"; break;
            case EncryptError::OutputExists:
                errMsg = "Output file already exists"; break;
            default:
                errMsg = QString::fromStdString(result.errorMessage);
                if (errMsg.isEmpty()) errMsg = "Encryption failed";
                break;
        }
        emit encryptionFailed(errMsg);
    }
}

} // namespace hlplayer::crypto