#include "QMLEncryptionExporter.h"
#include "EncryptionExporter.h"
#include <QMetaObject>
#include <memory>

namespace hlplayer {
namespace qml {

struct QMLEncryptionExporter::Impl {
    std::unique_ptr<crypto::EncryptionExporter> exporter;
    bool isProcessing = false;
    double progress = 0.0;
    QString lastError;
    QMLEncryptionExporter* owner = nullptr;
};

QMLEncryptionExporter::QMLEncryptionExporter(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {
    impl_->owner = this;
    impl_->exporter = std::make_unique<crypto::EncryptionExporter>();

    // Connect signals
    connect(impl_->exporter.get(), &crypto::EncryptionExporter::progressChanged,
            this, &QMLEncryptionExporter::handleProgressChanged);
    connect(impl_->exporter.get(), &crypto::EncryptionExporter::encryptionFinished,
            this, &QMLEncryptionExporter::handleEncryptionFinished);
    connect(impl_->exporter.get(), &crypto::EncryptionExporter::encryptionFailed,
            this, &QMLEncryptionExporter::handleEncryptionFailed);
}

QMLEncryptionExporter::~QMLEncryptionExporter() {
    if (impl_->isProcessing && impl_->exporter) {
        impl_->exporter->cancel();
    }
}

bool QMLEncryptionExporter::isProcessing() const {
    return impl_->isProcessing;
}

double QMLEncryptionExporter::progress() const {
    return impl_->progress;
}

QString QMLEncryptionExporter::lastError() const {
    return impl_->lastError;
}

bool QMLEncryptionExporter::isEncrypted(const QString& filePath) const {
    if (!impl_->exporter) return false;
    return impl_->exporter->isEncrypted(filePath);
}

void QMLEncryptionExporter::startEncryption(const QString& inputPath,
                                             const QString& outputPath,
                                             bool usePassword,
                                             const QString& password) {
    if (impl_->isProcessing) {
        emit errorOccurred(tr("Encryption already in progress"));
        return;
    }

    impl_->isProcessing = true;
    impl_->progress = 0.0;
    impl_->lastError.clear();
    emit stateChanged();
    emit progressChanged();

    if (!impl_->exporter) {
        impl_->isProcessing = false;
        impl_->lastError = tr("Encryption exporter not initialized");
        emit errorOccurred(impl_->lastError);
        emit stateChanged();
        return;
    }

    impl_->exporter->startEncryption(inputPath, outputPath, usePassword, password);
}

void QMLEncryptionExporter::cancel() {
    if (!impl_->isProcessing) return;

    if (impl_->exporter) {
        impl_->exporter->cancel();
    }

    impl_->isProcessing = false;
    impl_->lastError = tr("Cancelled by user");
    emit stateChanged();
}

void QMLEncryptionExporter::handleProgressChanged(double percent) {
    impl_->progress = percent;
    emit progressChanged();
}

void QMLEncryptionExporter::handleEncryptionFinished(const QString& keyString) {
    impl_->isProcessing = false;
    impl_->progress = 1.0;
    emit stateChanged();
    emit progressChanged();
    emit encryptionFinished(keyString);
}

void QMLEncryptionExporter::handleEncryptionFailed(const QString& error) {
    impl_->isProcessing = false;
    impl_->lastError = error;
    emit errorOccurred(error);
    emit stateChanged();
}

} // namespace qml
} // namespace hlplayer