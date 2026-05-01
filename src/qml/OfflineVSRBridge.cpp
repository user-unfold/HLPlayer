#include "OfflineVSRBridge.h"

#include <OfflineTranscodePipeline.h>
#include <hlplayer/logger.h>
#include <hlplayer/IVRAMBudgetManager.h>
#include <hlplayer/ICheckpointManager.h>
#ifdef HLPLAYER_VSR_ENABLED
#include <hlplayer/NcnnSuperResolution.h>
#endif
#include <FFmpegVideoDecoder.h>
#include <HWVideoEncoder.h>
#include <FFmpegMuxer.h>

#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QMetaObject>
#include <QUrl>

#ifdef _WIN32
    #define __STDC_CONSTANT_MACROS
    #define __STDC_FORMAT_MACROS
#endif

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace hlplayer {
namespace qml {

struct OfflineVSRBridge::Impl {
    int state = static_cast<int>(StudioState::Idle);

    double progress = 0.0;
    double currentFps = 0.0;
    double estimatedTimeRemaining = 0.0;
    QString currentStatus;

    qint64 vramUsedBytes = 0;
    qint64 vramTotalBytes = 0;

    int framesProcessed = 0;
    int totalFrames = 0;

    QString sourcePath;
    int sourceWidth = 0;
    int sourceHeight = 0;
    double sourceDuration = 0.0;
    double sourceFps = 0.0;
    Codec sourceCodec = Codec::Unknown;
    std::vector<uint8_t> sourceExtradata;

    int audioCodecId = 0;
    int audioSampleRate = 0;
    int audioChannels = 0;
    std::vector<uint8_t> audioExtradata;

    QString outputPath;
    int outputWidth = 0;
    int outputHeight = 0;

    double scaleFactor = 2.0;
    QString encoder = "auto";
    int quality = 23;
    QString preset = "medium";
    int threadCount = 0;
    QString performanceMode = "balanced";

    std::unique_ptr<OfflineTranscodePipeline> pipeline;
    std::thread pipelineThread;
    std::atomic<bool> pipelineRunning{false};

    QTimer* progressTimer = nullptr;

    std::shared_ptr<IVRAMBudgetManager> vramManager;

    OfflineVSRBridge* owner = nullptr;
    std::mutex stateMutex;

    void applyState(StudioState newState) {
        if (state != static_cast<int>(newState)) {
            state = static_cast<int>(newState);
            emit owner->stateChanged();
        }
    }

    void applyCompletion() {
        progressTimer->stop();
        progress = 100.0;
        currentFps = 0.0;
        estimatedTimeRemaining = 0.0;
        applyState(StudioState::Completed);
        currentStatus = QObject::tr("Completed");
        emit owner->statusChanged();
        emit owner->progressChanged();
        emit owner->fpsChanged();
        emit owner->etaChanged();
        emit owner->processingCompleted(outputPath);
        LOG_INFO("OfflineVSRBridge: processing completed -> '%s'",
                 outputPath.toStdString().c_str());
    }

    void applyError(const QString& message) {
        progressTimer->stop();
        applyState(StudioState::Error);
        currentStatus = QObject::tr("Error");
        emit owner->statusChanged();
        emit owner->errorOccurred(message);
        LOG_ERROR("OfflineVSRBridge: %s", message.toStdString().c_str());
    }

    void doPollProgress() {
        if (!pipeline) return;

        TranscodeProgress p = pipeline->getProgress();

        bool changed = false;

        if (framesProcessed != static_cast<int>(p.framesProcessed)) {
            framesProcessed = static_cast<int>(p.framesProcessed);
            changed = true;
        }
        if (totalFrames != static_cast<int>(p.totalFrames) && p.totalFrames > 0) {
            totalFrames = static_cast<int>(p.totalFrames);
            changed = true;
        }

        double newProgress = (p.totalFrames > 0)
            ? static_cast<double>(p.framesProcessed) / static_cast<double>(p.totalFrames) * 100.0
            : 0.0;
        if (std::abs(progress - newProgress) > 0.01) {
            progress = newProgress;
            changed = true;
        }

        if (std::abs(currentFps - p.currentFps) > 0.01) {
            currentFps = p.currentFps;
            emit owner->fpsChanged();
        }

        double newEta = p.estimatedSecondsLeft;
        if (std::abs(estimatedTimeRemaining - newEta) > 0.5) {
            estimatedTimeRemaining = newEta;
            emit owner->etaChanged();
        }

        if (changed) {
            emit owner->progressChanged();
        }

        if (vramManager) {
            qint64 used = static_cast<qint64>(vramManager->usedBytes());
            qint64 total = static_cast<qint64>(
                vramManager->usedBytes() + vramManager->availableBytes());
            if (vramUsedBytes != used || vramTotalBytes != total) {
                vramUsedBytes = used;
                vramTotalBytes = total;
                emit owner->vramChanged();
            }
        }
    }
};

namespace {

bool probeVideoInfo(const std::string& filePath,
                    int& outWidth, int& outHeight,
                    double& outDuration, double& outFps,
                    int& outTotalFrames,
                    Codec& outCodec,
                    std::vector<uint8_t>& outExtradata,
                    int& outAudioCodecId, int& outAudioSampleRate,
                    int& outAudioChannels, std::vector<uint8_t>& outAudioExtradata) {
    AVFormatContext* fmtCtx = nullptr;
    int ret = avformat_open_input(&fmtCtx, filePath.c_str(), nullptr, nullptr);
    if (ret < 0) return false;

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    int videoStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            videoStreamIdx = static_cast<int>(i);
            break;
        }
    }

    if (videoStreamIdx < 0) {
        avformat_close_input(&fmtCtx);
        return false;
    }

    AVStream* videoStream = fmtCtx->streams[videoStreamIdx];

    outWidth = videoStream->codecpar->width;
    outHeight = videoStream->codecpar->height;

    if (videoStream->duration > 0) {
        outDuration = videoStream->duration * av_q2d(videoStream->time_base);
    } else if (fmtCtx->duration > 0) {
        outDuration = fmtCtx->duration / static_cast<double>(AV_TIME_BASE);
    } else {
        outDuration = 0.0;
    }

    AVRational fr = av_guess_frame_rate(fmtCtx, videoStream, nullptr);
    outFps = (fr.num > 0 && fr.den > 0)
        ? static_cast<double>(fr.num) / static_cast<double>(fr.den)
        : 30.0;

    if (videoStream->nb_frames > 0) {
        outTotalFrames = static_cast<int>(videoStream->nb_frames);
    } else if (outDuration > 0.0 && outFps > 0.0) {
        outTotalFrames = static_cast<int>(outDuration * outFps);
    } else {
        outTotalFrames = 0;
    }

    // Detect codec
    AVCodecID codecId = videoStream->codecpar->codec_id;
    switch (codecId) {
        case AV_CODEC_ID_H264: outCodec = Codec::H264; break;
        case AV_CODEC_ID_HEVC: outCodec = Codec::HEVC; break;
        case AV_CODEC_ID_AV1:  outCodec = Codec::AV1; break;
        default:               outCodec = Codec::H264; break;
    }

    // Copy extradata
    if (videoStream->codecpar->extradata_size > 0) {
        outExtradata.assign(videoStream->codecpar->extradata,
                           videoStream->codecpar->extradata + videoStream->codecpar->extradata_size);
    }

    // Find audio stream
    int audioStreamIdx = -1;
    for (unsigned i = 0; i < fmtCtx->nb_streams; i++) {
        if (fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            audioStreamIdx = static_cast<int>(i);
            break;
        }
    }

    if (audioStreamIdx >= 0) {
        AVStream* audioStream = fmtCtx->streams[audioStreamIdx];
        outAudioCodecId = static_cast<int>(audioStream->codecpar->codec_id);
        outAudioSampleRate = audioStream->codecpar->sample_rate;
        outAudioChannels = audioStream->codecpar->ch_layout.nb_channels;
        if (audioStream->codecpar->extradata_size > 0) {
            outAudioExtradata.assign(audioStream->codecpar->extradata,
                                     audioStream->codecpar->extradata + audioStream->codecpar->extradata_size);
        }
    }

    avformat_close_input(&fmtCtx);
    return true;
}

Codec encoderStringToCodec(const QString& encoder) {
    QString enc = encoder.toLower();
    if (enc == "h264" || enc == "avc") return Codec::H264;
    if (enc == "h265" || enc == "hevc") return Codec::HEVC;
    if (enc == "av1") return Codec::AV1;
    return Codec::H264;
}

} // anonymous namespace

OfflineVSRBridge::OfflineVSRBridge(QObject* parent)
    : QObject(parent)
    , impl_(std::make_unique<Impl>()) {
    impl_->owner = this;

    impl_->progressTimer = new QTimer(this);
    impl_->progressTimer->setInterval(200);
    connect(impl_->progressTimer, &QTimer::timeout, this, [this]() {
        impl_->doPollProgress();
    });
}

OfflineVSRBridge::~OfflineVSRBridge() {
    if (impl_->pipeline) {
        impl_->pipeline->cancel();
    }
    impl_->pipelineRunning.store(false);

    if (impl_->progressTimer) {
        impl_->progressTimer->stop();
    }

    if (impl_->pipelineThread.joinable()) {
        impl_->pipelineThread.join();
    }

    impl_->pipeline.reset();
}

int OfflineVSRBridge::state() const {
    return impl_->state;
}

double OfflineVSRBridge::progress() const {
    return impl_->progress;
}

double OfflineVSRBridge::currentFps() const {
    return impl_->currentFps;
}

double OfflineVSRBridge::estimatedTimeRemaining() const {
    return impl_->estimatedTimeRemaining;
}

QString OfflineVSRBridge::currentStatus() const {
    return impl_->currentStatus;
}

qint64 OfflineVSRBridge::vramUsedBytes() const {
    return impl_->vramUsedBytes;
}

qint64 OfflineVSRBridge::vramTotalBytes() const {
    return impl_->vramTotalBytes;
}

int OfflineVSRBridge::framesProcessed() const {
    return impl_->framesProcessed;
}

int OfflineVSRBridge::totalFrames() const {
    return impl_->totalFrames;
}

QString OfflineVSRBridge::sourcePath() const {
    return impl_->sourcePath;
}

int OfflineVSRBridge::sourceWidth() const {
    return impl_->sourceWidth;
}

int OfflineVSRBridge::sourceHeight() const {
    return impl_->sourceHeight;
}

double OfflineVSRBridge::sourceDuration() const {
    return impl_->sourceDuration;
}

double OfflineVSRBridge::sourceFps() const {
    return impl_->sourceFps;
}

QString OfflineVSRBridge::outputPath() const {
    return impl_->outputPath;
}

void OfflineVSRBridge::setOutputPath(const QString& path) {
    if (impl_->state == static_cast<int>(StudioState::Processing) ||
        impl_->state == static_cast<int>(StudioState::Paused)) {
        emit errorOccurred(tr("Cannot change output path while processing"));
        return;
    }
    if (impl_->outputPath != path) {
        impl_->outputPath = path;
        emit outputPathChanged();
    }
}

int OfflineVSRBridge::outputWidth() const {
    return impl_->outputWidth;
}

int OfflineVSRBridge::outputHeight() const {
    return impl_->outputHeight;
}

void OfflineVSRBridge::importVideo(const QString& path) {
    LOG_INFO("OfflineVSRBridge::importVideo called with path='%s'", path.toStdString().c_str());
    QUrl url(path);
    QString localPath = url.isLocalFile() ? url.toLocalFile() : path;
    QFileInfo fi(localPath);
    if (!fi.exists() || !fi.isFile()) {
        emit errorOccurred(tr("File does not exist: %1").arg(localPath));
        return;
    }

    QString suffix = fi.suffix().toLower();
    QStringList videoExts = {"mp4", "mkv", "avi", "mov", "wmv", "flv", "webm", "ts", "m4v"};
    if (!videoExts.contains(suffix)) {
        emit errorOccurred(tr("Unsupported video format: %1").arg(suffix));
        return;
    }

    int w = 0, h = 0, totalFrames = 0;
    double duration = 0.0, fps = 0.0;
    Codec sourceCodec = Codec::Unknown;
    std::vector<uint8_t> sourceExtradata;
    int audioCodecId = 0, audioSampleRate = 0, audioChannels = 0;
    std::vector<uint8_t> audioExtradata;

    if (!probeVideoInfo(localPath.toStdString(), w, h, duration, fps, totalFrames,
                        sourceCodec, sourceExtradata,
                        audioCodecId, audioSampleRate, audioChannels, audioExtradata)) {
        emit errorOccurred(tr("Failed to probe video file: %1").arg(localPath));
        return;
    }

    if (w <= 0 || h <= 0) {
        emit errorOccurred(tr("Invalid video dimensions: %1x%2").arg(w).arg(h));
        return;
    }

    impl_->progress = 0.0;
    impl_->currentFps = 0.0;
    impl_->estimatedTimeRemaining = 0.0;
    impl_->framesProcessed = 0;
    impl_->vramUsedBytes = 0;

    impl_->sourcePath = localPath;
    impl_->sourceWidth = w;
    impl_->sourceHeight = h;
    impl_->sourceDuration = duration;
    impl_->sourceFps = fps;
    impl_->sourceCodec = sourceCodec;
    impl_->sourceExtradata = std::move(sourceExtradata);
    impl_->totalFrames = totalFrames;
    impl_->audioCodecId = audioCodecId;
    impl_->audioSampleRate = audioSampleRate;
    impl_->audioChannels = audioChannels;
    impl_->audioExtradata = std::move(audioExtradata);

    impl_->outputWidth = static_cast<int>(w * impl_->scaleFactor);
    impl_->outputHeight = static_cast<int>(h * impl_->scaleFactor);

    if (impl_->outputPath.isEmpty()) {
        QString baseName = fi.completeBaseName();
        QString dir = fi.absolutePath();
        impl_->outputPath = dir + "/" + baseName + "_vsr.mp4";
    }

    impl_->currentStatus = tr("Ready — %1x%2, %3 fps, %4s")
        .arg(w).arg(h)
        .arg(fps, 0, 'f', 1)
        .arg(duration, 0, 'f', 1);

    impl_->applyState(StudioState::Ready);

    LOG_INFO("OfflineVSRBridge: imported video '%s' (%dx%d, %.1ffps, %.1fs, %d frames)",
             localPath.toStdString().c_str(), w, h, fps, duration, totalFrames);

    emit sourceInfoChanged();
    emit outputInfoChanged();
    emit outputPathChanged();
    emit progressChanged();
    emit fpsChanged();
    emit etaChanged();
    emit vramChanged();
    emit statusChanged();
}

void OfflineVSRBridge::startProcessing() {
    std::lock_guard<std::mutex> lock(impl_->stateMutex);

    auto curState = static_cast<StudioState>(impl_->state);
    if (curState != StudioState::Ready && curState != StudioState::Completed) {
        emit errorOccurred(tr("Cannot start processing in current state"));
        return;
    }

    if (impl_->sourcePath.isEmpty()) {
        emit errorOccurred(tr("No video imported"));
        return;
    }

    if (impl_->outputPath.isEmpty()) {
        emit errorOccurred(tr("Output path not set"));
        return;
    }

    try {
        impl_->pipeline = std::make_unique<OfflineTranscodePipeline>();

        impl_->pipeline->setProgressCallback(
            [this](const TranscodeProgress& p) {
                QMetaObject::invokeMethod(this, [this, p]() {
                    bool changed = false;

                    if (impl_->framesProcessed != static_cast<int>(p.framesProcessed)) {
                        impl_->framesProcessed = static_cast<int>(p.framesProcessed);
                        changed = true;
                    }
                    if (impl_->totalFrames != static_cast<int>(p.totalFrames) && p.totalFrames > 0) {
                        impl_->totalFrames = static_cast<int>(p.totalFrames);
                        changed = true;
                    }

                    double newProgress = (p.totalFrames > 0)
                        ? static_cast<double>(p.framesProcessed) / static_cast<double>(p.totalFrames) * 100.0
                        : 0.0;
                    if (std::abs(impl_->progress - newProgress) > 0.01) {
                        impl_->progress = newProgress;
                        changed = true;
                    }

                    if (std::abs(impl_->currentFps - p.currentFps) > 0.01) {
                        impl_->currentFps = p.currentFps;
                        emit fpsChanged();
                    }

                    double newEta = p.estimatedSecondsLeft;
                    if (std::abs(impl_->estimatedTimeRemaining - newEta) > 0.5) {
                        impl_->estimatedTimeRemaining = newEta;
                        emit etaChanged();
                    }

                    QString newStatus = QString::fromStdString(p.stage);
                    if (impl_->currentStatus != newStatus) {
                        impl_->currentStatus = newStatus;
                        emit statusChanged();
                    }

                    if (changed) {
                        emit progressChanged();
                    }

                    if (p.isComplete) {
                        impl_->applyCompletion();
                    }
                }, Qt::QueuedConnection);
            }
        );

        impl_->pipeline->setErrorCallback(
            [this](PlayerError, const std::string& msg) {
                QMetaObject::invokeMethod(this, [this, msg]() {
                    impl_->applyError(QString::fromStdString(msg));
                }, Qt::QueuedConnection);
            }
        );

        OfflineTranscodeConfig config;
        config.inputPath = impl_->sourcePath.toStdString();
        config.outputPath = impl_->outputPath.toStdString();
        config.outputFormat = "mp4";
        config.fastStart = true;
        config.audioPassthrough = (impl_->audioCodecId != 0);
        config.audioCodecId = impl_->audioCodecId;
        config.audioSampleRate = impl_->audioSampleRate;
        config.audioChannels = impl_->audioChannels;
        config.audioExtradata = impl_->audioExtradata;
        config.vsrScaleFactor = static_cast<int>(impl_->scaleFactor);
        config.decodeBackend = DecodeBackend::Auto;

        int scaleFactor = static_cast<int>(impl_->scaleFactor);
        int outputWidth = impl_->sourceWidth * scaleFactor;
        int outputHeight = impl_->sourceHeight * scaleFactor;

        config.encoderConfig.codec = encoderStringToCodec(impl_->encoder);
        config.encoderConfig.width = static_cast<uint32_t>(outputWidth);
        config.encoderConfig.height = static_cast<uint32_t>(outputHeight);
        config.encoderConfig.frameRate = impl_->sourceFps;
        config.encoderConfig.crf = static_cast<uint32_t>(impl_->quality);
        config.encoderConfig.preset = impl_->preset.toStdString();
        config.encoderConfig.hwAccel = HwAccelMode::Auto;

        if (impl_->vramManager) {
            impl_->pipeline->setVRAMBudgetManager(impl_->vramManager);
        }

#ifdef HLPLAYER_VSR_ENABLED
        NcnnSRConfig vsrConfig;
        vsrConfig.modelPath = "D:/HLPlayer/models/realesrgan-x2plus.ncnn";
        vsrConfig.scaleFactor = scaleFactor;
        vsrConfig.vramBudgetManager = impl_->vramManager;

        auto vsrNode = std::make_shared<NcnnSuperResolution>(vsrConfig);
        auto vsrInitResult = vsrNode->initialize();
        if (vsrInitResult.hasError()) {
            LOG_WARN("OfflineVSRBridge: VSR node init failed, continuing without VSR");
        } else {
            impl_->pipeline->setVSRNode(vsrNode);
            LOG_INFO("OfflineVSRBridge: VSR node initialized");
        }
#endif

        auto decoder = std::make_shared<extractor::FFmpegVideoDecoder>();
        DecoderConfig decConfig;
        decConfig.backend = DecodeBackend::CPU;
        decConfig.codec = impl_->sourceCodec;
        decConfig.width = static_cast<uint32_t>(impl_->sourceWidth);
        decConfig.height = static_cast<uint32_t>(impl_->sourceHeight);
        decConfig.outputPixelFormat = PixelFormat::NV12;
        decConfig.extradata = impl_->sourceExtradata;
        auto decOpenResult = decoder->open(decConfig);
        if (decOpenResult.hasError()) {
            QString errMsg = tr("Failed to open decoder (error %1)")
                .arg(static_cast<int>(decOpenResult.error()));
            LOG_ERROR("OfflineVSRBridge: %s", errMsg.toStdString().c_str());
            emit errorOccurred(errMsg);
            impl_->pipeline.reset();
            impl_->applyState(StudioState::Error);
            return;
        }
        impl_->pipeline->setDecoder(decoder);

        // --- Create and inject encoder ---
        auto encoder = std::make_shared<extractor::HWVideoEncoder>();
        EncoderConfig encConfig;
        encConfig.codec = encoderStringToCodec(impl_->encoder);
        encConfig.width = static_cast<uint32_t>(outputWidth);
        encConfig.height = static_cast<uint32_t>(outputHeight);
        encConfig.frameRate = impl_->sourceFps;
        encConfig.crf = static_cast<uint32_t>(impl_->quality);
        encConfig.preset = impl_->preset.toStdString();
        encConfig.hwAccel = HwAccelMode::None;
        encConfig.inputFormat = PixelFormat::NV12;
        auto encOpenResult = encoder->open(encConfig);
        if (encOpenResult.hasError()) {
            QString errMsg = tr("Failed to open encoder (error %1)")
                .arg(static_cast<int>(encOpenResult.error()));
            LOG_ERROR("OfflineVSRBridge: %s", errMsg.toStdString().c_str());
            emit errorOccurred(errMsg);
            impl_->pipeline.reset();
            impl_->applyState(StudioState::Error);
            return;
        }
        impl_->pipeline->setEncoder(encoder);

        // --- Create and inject muxer ---
        auto muxer = std::make_shared<extractor::FFmpegMuxer>();
        impl_->pipeline->setMuxer(muxer);

        auto configResult = impl_->pipeline->configure(config);
        if (configResult.hasError()) {
            QString errMsg = tr("Pipeline configuration failed (error %1)")
                .arg(static_cast<int>(configResult.error()));
            LOG_ERROR("OfflineVSRBridge: %s", errMsg.toStdString().c_str());
            emit errorOccurred(errMsg);
            impl_->pipeline.reset();
            impl_->applyState(StudioState::Error);
            return;
        }

        impl_->applyState(StudioState::Processing);
        impl_->currentStatus = tr("Starting...");
        emit statusChanged();

        impl_->pipelineRunning.store(true);
        impl_->pipelineThread = std::thread([this]() {
            auto startResult = impl_->pipeline->start();
            if (startResult.hasError()) {
                QString errMsg = tr("Pipeline start failed (error %1)")
                    .arg(static_cast<int>(startResult.error()));
                QMetaObject::invokeMethod(this, [this, errMsg]() {
                    impl_->applyError(errMsg);
                }, Qt::QueuedConnection);
                return;
            }

            auto waitResult = impl_->pipeline->waitUntilComplete();
            if (waitResult.hasError()) {
                QString errMsg = tr("Processing failed (error %1)")
                    .arg(static_cast<int>(waitResult.error()));
                QMetaObject::invokeMethod(this, [this, errMsg]() {
                    impl_->applyError(errMsg);
                }, Qt::QueuedConnection);
                return;
            }

            auto finalState = impl_->pipeline->getState();
            if (finalState == OfflineTranscodePipeline::State::Completed) {
                QMetaObject::invokeMethod(this, [this]() {
                    impl_->applyCompletion();
                }, Qt::QueuedConnection);
            } else if (finalState == OfflineTranscodePipeline::State::Cancelled) {
                QMetaObject::invokeMethod(this, [this]() {
                    impl_->applyState(StudioState::Idle);
                    impl_->currentStatus = tr("Cancelled");
                    emit statusChanged();
                    impl_->progressTimer->stop();
                    impl_->pipelineRunning.store(false);
                }, Qt::QueuedConnection);
            }

            impl_->pipelineRunning.store(false);
        });

        impl_->progressTimer->start();

        LOG_INFO("OfflineVSRBridge: processing started for '%s'",
                 impl_->sourcePath.toStdString().c_str());

    } catch (const std::exception& e) {
        QString errMsg = tr("Failed to start processing: %1").arg(e.what());
        LOG_ERROR("OfflineVSRBridge: %s", errMsg.toStdString().c_str());
        emit errorOccurred(errMsg);
        impl_->applyState(StudioState::Error);
        impl_->pipeline.reset();
    }
}

void OfflineVSRBridge::pauseProcessing() {
    if (impl_->state != static_cast<int>(StudioState::Processing)) {
        return;
    }

    if (impl_->pipeline) {
        impl_->pipeline->pause();
    }

    impl_->applyState(StudioState::Paused);
    impl_->currentStatus = tr("Paused");
    emit statusChanged();
    impl_->progressTimer->stop();

    LOG_INFO("OfflineVSRBridge: processing paused");
}

void OfflineVSRBridge::resumeProcessing() {
    if (impl_->state != static_cast<int>(StudioState::Paused)) {
        return;
    }

    if (impl_->pipeline) {
        impl_->pipeline->resume();
    }

    impl_->applyState(StudioState::Processing);
    impl_->currentStatus = tr("Processing...");
    emit statusChanged();
    impl_->progressTimer->start();

    LOG_INFO("OfflineVSRBridge: processing resumed");
}

void OfflineVSRBridge::cancelProcessing() {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState != StudioState::Processing && curState != StudioState::Paused) {
        return;
    }

    impl_->progressTimer->stop();

    if (impl_->pipeline) {
        impl_->pipeline->cancel();
    }

    impl_->progress = 0.0;
    impl_->currentFps = 0.0;
    impl_->estimatedTimeRemaining = 0.0;
    impl_->framesProcessed = 0;

    impl_->applyState(StudioState::Idle);
    impl_->currentStatus = tr("Cancelled");
    emit statusChanged();
    emit progressChanged();
    emit fpsChanged();
    emit etaChanged();

    LOG_INFO("OfflineVSRBridge: processing cancelled");
}

void OfflineVSRBridge::setScaleFactor(double factor) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change scale factor while processing"));
        return;
    }

    factor = qBound(1.0, factor, 4.0);
    impl_->scaleFactor = factor;

    if (impl_->sourceWidth > 0 && impl_->sourceHeight > 0) {
        impl_->outputWidth = static_cast<int>(impl_->sourceWidth * factor);
        impl_->outputHeight = static_cast<int>(impl_->sourceHeight * factor);
        emit outputInfoChanged();
    }
}

void OfflineVSRBridge::setEncoder(const QString& encoder) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change encoder while processing"));
        return;
    }

    QString enc = encoder.toLower();
    QStringList valid = {"auto", "h264", "hevc", "h265", "av1", "avc"};
    if (!valid.contains(enc)) {
        emit errorOccurred(tr("Unsupported encoder: %1").arg(encoder));
        return;
    }

    impl_->encoder = enc;
}

void OfflineVSRBridge::setQuality(int quality) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change quality while processing"));
        return;
    }

    quality = qBound(0, quality, 51);
    impl_->quality = quality;
}

void OfflineVSRBridge::setPreset(const QString& preset) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change preset while processing"));
        return;
    }

    impl_->preset = preset.toLower();
}

void OfflineVSRBridge::setThreadCount(int count) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change thread count while processing"));
        return;
    }

    impl_->threadCount = qMax(0, count);
}

void OfflineVSRBridge::setPerformanceMode(const QString& mode) {
    auto curState = static_cast<StudioState>(impl_->state);
    if (curState == StudioState::Processing || curState == StudioState::Paused) {
        emit errorOccurred(tr("Cannot change performance mode while processing"));
        return;
    }

    QString m = mode.toLower();
    if (m != "performance" && m != "balanced") {
        emit errorOccurred(tr("Invalid performance mode: %1 (use 'performance' or 'balanced')").arg(mode));
        return;
    }

    impl_->performanceMode = m;

    if (impl_->vramManager) {
        auto pm = (m == "performance")
            ? PerformanceMode::Performance
            : PerformanceMode::Balanced;
        impl_->vramManager->setPerformanceMode(pm);
    }
}

} // namespace qml
} // namespace hlplayer
