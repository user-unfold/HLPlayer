#ifndef HLPLAYER_PREVIEWRENDERER_H
#define HLPLAYER_PREVIEWRENDERER_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/Result.h>
#include <cstdint>
#include <vector>
#include <mutex>
#include <chrono>

#ifdef BUILD_QML
#include <QQuickImageProvider>
#include <QImage>
#endif

struct SwsContext;

namespace hlplayer {

#ifdef BUILD_QML
/// QML image provider for camera preview frames.
class HLPLAYER_CAMERA_API CameraPreviewProvider : public QQuickImageProvider {
public:
    CameraPreviewProvider();
    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

    void setFrame(const QImage& frame);
    QImage getCurrentFrame() const;

private:
    QImage currentFrame_;
    mutable std::mutex frameMutex_;
};
#endif

/// Preview renderer for camera frames.
/// Converts YUV frames to RGBA and provides them to QML for display.
///
/// Uses either an engine-registered provider (set via setEngineProvider)
/// or creates a local fallback. The engine-registered approach avoids
/// ownership issues with QQmlEngine::addImageProvider.
class HLPLAYER_CAMERA_API PreviewRenderer {
public:
    PreviewRenderer();
    ~PreviewRenderer();

    Result<void> init(int width, int height);

    void onFrame(const uint8_t* data, int width, int height, int stride);

    /// Reinitialize the sws context for a different source pixel format.
    /// Called automatically by onFrame when the format changes.
    /// Safe to call before init() has been called.
    void setSourceFormat(int avPixelFormat);

    void stop();

#ifdef BUILD_QML
    /// Set the image provider registered with the QML engine.
    /// Must be called before init(). The engine owns this provider;
    /// PreviewRenderer uses a non-owning pointer.
    static void setEngineProvider(CameraPreviewProvider* provider);

    /// Get the image provider for QML integration.
    CameraPreviewProvider* getImageProvider() { return activeProvider_; }
#endif

private:
    void cleanup();
    Result<void> rebuildSwsContext();

    int width_ = 0;
    int height_ = 0;
    int srcFmt_ = 0;
    SwsContext* swsContext_ = nullptr;

#ifdef BUILD_QML
    CameraPreviewProvider* activeProvider_ = nullptr;
    std::unique_ptr<CameraPreviewProvider> localProvider_;
    static CameraPreviewProvider* s_engineProvider_;
#endif

    std::vector<uint8_t> backBuffer_;
    std::vector<uint8_t> frontBuffer_;
    int bufferStride_ = 0;

    std::mutex bufferMutex_;

    std::chrono::steady_clock::time_point lastFrameTime_;
    static constexpr int TARGET_FPS = 30;
    static constexpr int MIN_FRAME_INTERVAL_MS = 1000 / TARGET_FPS;
};

} // namespace hlplayer

#endif // HLPLAYER_PREVIEWRENDERER_H
