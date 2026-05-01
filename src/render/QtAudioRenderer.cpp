#include "QtAudioRenderer.h"

#include <spdlog/spdlog.h>
#include <QMetaObject>

namespace hlplayer {
namespace render {

QtAudioRenderer::QtAudioRenderer() = default;

QtAudioRenderer::~QtAudioRenderer() {
    close();
}

QAudioFormat::SampleFormat QtAudioRenderer::toQSampleFormat(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::S16:   return QAudioFormat::Int16;
        case AudioSampleFormat::S32:   return QAudioFormat::Int32;
        case AudioSampleFormat::Float: return QAudioFormat::Float;
        case AudioSampleFormat::U8:    return QAudioFormat::UInt8;
        default:                       return QAudioFormat::Int16;
    }
}

AudioSampleFormat QtAudioRenderer::fromQSampleFormat(QAudioFormat::SampleFormat fmt) const {
    switch (fmt) {
        case QAudioFormat::Int16: return AudioSampleFormat::S16;
        case QAudioFormat::Int32: return AudioSampleFormat::S32;
        case QAudioFormat::Float: return AudioSampleFormat::Float;
        case QAudioFormat::UInt8: return AudioSampleFormat::U8;
        default: return AudioSampleFormat::S16;
    }
}

int QtAudioRenderer::bytesPerSample(AudioSampleFormat fmt) const {
    switch (fmt) {
        case AudioSampleFormat::U8:    return 1;
        case AudioSampleFormat::S16:   return 2;
        case AudioSampleFormat::S32:   return 4;
        case AudioSampleFormat::Float: return 4;
        default:                       return 2;
    }
}

QAudioFormat QtAudioRenderer::toQAudioFormat(const AudioFormat& format) const {
    QAudioFormat qfmt;
    qfmt.setSampleRate(format.sampleRate);
    qfmt.setChannelCount(format.channels);
    qfmt.setSampleFormat(toQSampleFormat(format.sampleFormat));
    return qfmt;
}

bool QtAudioRenderer::open(const AudioFormat& format) {
    if (isOpen_.load()) {
        close();
    }

    QAudioDevice outputDevice = QMediaDevices::defaultAudioOutput();
    if (outputDevice.isNull()) {
        spdlog::warn("QtAudioRenderer: no audio output device available");
        return false;
    }

    QAudioFormat qfmt = toQAudioFormat(format);
    if (!outputDevice.isFormatSupported(qfmt)) {
        spdlog::warn("QtAudioRenderer: format not supported, trying nearest");
        qfmt = outputDevice.preferredFormat();
    }

    audioSink_ = std::make_unique<QAudioSink>(outputDevice, qfmt);
    if (!audioSink_) {
        spdlog::error("QtAudioRenderer: failed to create QAudioSink");
        return false;
    }

    ioDevice_ = audioSink_->start();
    if (!ioDevice_) {
        spdlog::error("QtAudioRenderer: failed to start QAudioSink");
        audioSink_.reset();
        return false;
    }

    format_.sampleRate = qfmt.sampleRate();
    format_.channels = qfmt.channelCount();
    format_.sampleFormat = fromQSampleFormat(qfmt.sampleFormat());
    format_.bytesPerSample = bytesPerSample(format_.sampleFormat);
    isOpen_.store(true);

    spdlog::info("QtAudioRenderer: opened sampleRate={} channels={} format={}",
                 qfmt.sampleRate(), qfmt.channelCount(),
                 static_cast<int>(format.sampleFormat));
    return true;
}

void QtAudioRenderer::write(const uint8_t* data, size_t size) {
    if (!isOpen_.load() || !ioDevice_ || !audioSink_) {
        return;
    }

    if (QThread::currentThread() == ioDevice_->thread()) {
        qint64 written = ioDevice_->write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
        if (written < 0) {
            spdlog::warn("QtAudioRenderer: write error");
        }
        return;
    }

    QByteArray buffer(reinterpret_cast<const char*>(data), static_cast<int>(size));
    QPointer<QIODevice> device = ioDevice_;
    QPointer<QAudioSink> sink = audioSink_.get();
    
    QMetaObject::invokeMethod(ioDevice_, [device, sink, buffer]() mutable {
        if (!device || !sink) return;
        int offset = 0;
        int remaining = buffer.size();
        while (remaining > 0 && device) {
            int toWrite = std::min(remaining, static_cast<int>(sink->bytesFree()));
            if (toWrite > 0) {
                qint64 written = device->write(buffer.data() + offset, toWrite);
                if (written > 0) {
                    offset += written;
                    remaining -= written;
                }
            }
            if (remaining > 0) {
                QThread::msleep(5);
            }
        }
    }, Qt::BlockingQueuedConnection);
}

void QtAudioRenderer::pause() {
    if (!audioSink_ || !isOpen_.load()) {
        return;
    }

    if (QThread::currentThread() == audioSink_->thread()) {
        audioSink_->suspend();
        return;
    }

    QPointer<QAudioSink> sink = audioSink_.get();
    QMetaObject::invokeMethod(audioSink_.get(), [sink]() {
        if (sink) {
            sink->suspend();
        }
    }, Qt::QueuedConnection);
}

void QtAudioRenderer::resume() {
    if (!audioSink_ || !isOpen_.load()) {
        return;
    }

    if (QThread::currentThread() == audioSink_->thread()) {
        audioSink_->resume();
        return;
    }

    QPointer<QAudioSink> sink = audioSink_.get();
    QMetaObject::invokeMethod(audioSink_.get(), [sink]() {
        if (sink) {
            sink->resume();
        }
    }, Qt::QueuedConnection);
}

void QtAudioRenderer::close() {
    if (audioSink_) {
        if (QThread::currentThread() == audioSink_->thread()) {
            audioSink_->stop();
            audioSink_.reset();
        } else {
            QPointer<QAudioSink> sink = audioSink_.get();
            QMetaObject::invokeMethod(audioSink_.get(), [sink]() {
                if (sink) {
                    sink->stop();
                }
            }, Qt::QueuedConnection);
            audioSink_.reset();
        }
    }
    ioDevice_ = nullptr;
    isOpen_.store(false);
}

AudioFormat QtAudioRenderer::format() const {
    return format_;
}

int QtAudioRenderer::getLatencyMs() const {
    if (!audioSink_) {
        return 0;
    }
    return static_cast<int>(audioSink_->bufferSize() /
           (format_.bytesPerSample * format_.channels * format_.sampleRate / 1000));
}

} // namespace render
} // namespace hlplayer
