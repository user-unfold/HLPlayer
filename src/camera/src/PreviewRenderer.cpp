#include "hlplayer/PreviewRenderer.h"
#include <spdlog/spdlog.h>
#include <algorithm>

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/pixfmt.h>
#include <libavutil/pixdesc.h>
}

#ifdef BUILD_QML
#include <QtGui/QImage>
#endif

namespace hlplayer {

#ifdef BUILD_QML

CameraPreviewProvider* PreviewRenderer::s_engineProvider_ = nullptr;

void PreviewRenderer::setEngineProvider(CameraPreviewProvider* provider) {
    s_engineProvider_ = provider;
}

CameraPreviewProvider::CameraPreviewProvider()
    : QQuickImageProvider(QQuickImageProvider::Image) {
}

QImage CameraPreviewProvider::requestImage(const QString& /*id*/, QSize* size, const QSize& requestedSize) {
    std::lock_guard<std::mutex> lock(frameMutex_);

    static int reqCount = 0;
    bool shouldLog = (++reqCount <= 5) || (reqCount % 30 == 1);
    if (shouldLog)
        spdlog::info("PreviewProvider: requestImage #{} isNull={} size={}x{} reqSize={}x{}",
                     reqCount, currentFrame_.isNull(),
                     currentFrame_.isNull() ? 0 : currentFrame_.width(),
                     currentFrame_.isNull() ? 0 : currentFrame_.height(),
                     requestedSize.width(), requestedSize.height());

    if (currentFrame_.isNull()) {
        if (shouldLog)
            spdlog::warn("PreviewProvider: no frame available yet, returning black placeholder");
        if (size) *size = requestedSize.isEmpty() ? QSize(640, 480) : requestedSize;
        QImage black(640, 480, QImage::Format_RGB32);
        black.fill(Qt::black);
        if (!requestedSize.isEmpty()) {
            black = black.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        return black;
    }

    if (size) {
        *size = currentFrame_.size();
    }

    if (!requestedSize.isEmpty() && requestedSize != currentFrame_.size()) {
        return currentFrame_.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    return currentFrame_;
}

void CameraPreviewProvider::setFrame(const QImage& frame) {
    std::lock_guard<std::mutex> lock(frameMutex_);
    currentFrame_ = frame;
}

QImage CameraPreviewProvider::getCurrentFrame() const {
    std::lock_guard<std::mutex> lock(frameMutex_);
    return currentFrame_;
}

#endif

PreviewRenderer::PreviewRenderer() {
}

PreviewRenderer::~PreviewRenderer() {
    cleanup();
}

Result<void> PreviewRenderer::init(int width, int height) {
    if (width <= 0 || height <= 0) {
        spdlog::error("PreviewRenderer::init: invalid dimensions {}x{}", width, height);
        return Result<void>::error(PlayerError::InvalidState);
    }

    width_ = width;
    height_ = height;
    bufferStride_ = width * 4;

    backBuffer_.resize(static_cast<size_t>(bufferStride_ * height));
    frontBuffer_.resize(static_cast<size_t>(bufferStride_ * height));

#ifdef BUILD_QML
    if (s_engineProvider_) {
        activeProvider_ = s_engineProvider_;
    } else {
        localProvider_ = std::make_unique<CameraPreviewProvider>();
        activeProvider_ = localProvider_.get();
    }
#endif

    if (srcFmt_ <= 0)
        srcFmt_ = AV_PIX_FMT_YUV420P;
    return rebuildSwsContext();
}

Result<void> PreviewRenderer::rebuildSwsContext() {
    if (swsContext_) {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }

    swsContext_ = sws_getContext(
        width_, height_, static_cast<AVPixelFormat>(srcFmt_),
        width_, height_, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!swsContext_) {
        spdlog::error("PreviewRenderer: sws_getContext failed for {}→RGBA",
                      av_get_pix_fmt_name(static_cast<AVPixelFormat>(srcFmt_)));
        return Result<void>::error(PlayerError::DecodeError);
    }

    spdlog::info("PreviewRenderer initialized for {}x{} fmt={}", width_, height_,
                 av_get_pix_fmt_name(static_cast<AVPixelFormat>(srcFmt_)));
    return Result<void>::success();
}

void PreviewRenderer::setSourceFormat(int avPixelFormat) {
    if (avPixelFormat == srcFmt_)
        return;
    srcFmt_ = avPixelFormat;
    if (width_ > 0 && height_ > 0)
        rebuildSwsContext();
}

void PreviewRenderer::onFrame(const uint8_t* data, int width, int height, int stride) {
    if (!swsContext_) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime_).count();

    if (elapsed < MIN_FRAME_INTERVAL_MS) {
        return;
    }

    lastFrameTime_ = now;

    if (width != width_ || height != height_) {
        spdlog::warn("PreviewRenderer::onFrame: frame size mismatch {}x{} vs {}x{}, reinitializing",
                     width, height, width_, height_);
        init(width, height);
    }

    const uint8_t* srcData[4] = { data, nullptr, nullptr, nullptr };
    int srcStrides[4] = { stride, 0, 0, 0 };

    uint8_t* dstData[4] = { backBuffer_.data(), nullptr, nullptr, nullptr };
    int dstStrides[4] = { bufferStride_, 0, 0, 0 };

    sws_scale(swsContext_, srcData, srcStrides, 0, height, dstData, dstStrides);

    {
        std::lock_guard<std::mutex> lock(bufferMutex_);
        std::swap(backBuffer_, frontBuffer_);
    }

#ifdef BUILD_QML
    if (activeProvider_) {
        QImage img(frontBuffer_.data(), width_, height_, bufferStride_, QImage::Format_RGBA8888);
        bool valid = !img.isNull();
        activeProvider_->setFrame(img.copy());
        static int previewFrameCount = 0;
        bool shouldLog = (++previewFrameCount <= 5) || (previewFrameCount % 30 == 1);
        if (shouldLog)
            spdlog::info("PreviewRenderer: delivered {} frames to QML (valid={} size={}x{})",
                         previewFrameCount, valid, img.width(), img.height());
    }
#endif
}

void PreviewRenderer::stop() {
    cleanup();
}

void PreviewRenderer::cleanup() {
    if (swsContext_) {
        sws_freeContext(swsContext_);
        swsContext_ = nullptr;
    }

    backBuffer_.clear();
    frontBuffer_.clear();

#ifdef BUILD_QML
    if (activeProvider_) {
        activeProvider_->setFrame(QImage());
    }
    activeProvider_ = nullptr;
    localProvider_.reset();
#endif

    width_ = 0;
    height_ = 0;
    srcFmt_ = 0;
    bufferStride_ = 0;
}

} // namespace hlplayer
