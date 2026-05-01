#include "VideoOutputItem.h"

#include <QtQuick/QSGSimpleTextureNode>
#include <QtQuick/QQuickWindow>

#include <QImage>

#include <spdlog/spdlog.h>

#ifdef _WIN32
#include <d3d11.h>
#include <QtQuick/QSGRendererInterface>
#include <QtQuick/qsgtexture_platform.h>
#include "../extractor/HWDevicePool.h"
#endif

namespace hlplayer {
namespace qml {

VideoOutputItem::VideoOutputItem(QQuickItem* parent)
    : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    spdlog::info("VideoOutputItem constructed");
}

VideoOutputItem::~VideoOutputItem() {
    if (videoSink_) {
        videoSink_->onFrameAvailable = nullptr;
    }
    spdlog::info("VideoOutputItem destructed");
}

render::VulkanVideoSink* VideoOutputItem::videoSink() const {
    return videoSink_;
}

void VideoOutputItem::setVideoSink(render::VulkanVideoSink* sink) {
    if (videoSink_ == sink) return;

    if (videoSink_) {
        videoSink_->onFrameAvailable = nullptr;
    }

    videoSink_ = sink;

    if (videoSink_) {
        videoSink_->onFrameAvailable = [this]() {
            copyPendingFrame();
            QMetaObject::invokeMethod(this, "update", Qt::QueuedConnection);
        };
    }

    emit videoSinkChanged();
    update();
}

#ifdef _WIN32
void VideoOutputItem::setD3D11Bridge(hlplayer::render::D3D11RenderBridge* bridge) {
    d3d11Bridge_ = bridge;
    if (bridge) {
        spdlog::info("VideoOutputItem: D3D11RenderBridge connected");
    }
}
#endif

void VideoOutputItem::copyPendingFrame() {
    if (!videoSink_) return;

    uint32_t w = videoSink_->frameWidth();
    uint32_t h = videoSink_->frameHeight();
    if (w == 0 || h == 0) return;

    const uint8_t* data = videoSink_->getCpuFrameData();
    size_t size = videoSink_->getCpuFrameDataSize();
    if (!data || size == 0) return;

    std::lock_guard<std::mutex> lock(frameMutex_);
    pendingFrameData_.assign(data, data + size);
    pendingWidth_ = w;
    pendingHeight_ = h;
    frameReady_ = true;
}

QSGNode* VideoOutputItem::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) {
    Q_UNUSED(data);

    // D3D11 device sharing disabled — conflicts with active decoder thread.
    // Re-enable only when zero-copy D3D11RenderBridge rendering is wired up.

#ifdef _WIN32
    if (d3d11Bridge_ && d3d11Bridge_->getCurrentTexture()) {
        ID3D11Texture2D* nativeTex = d3d11Bridge_->getCurrentTexture();
        uint32_t texW = 0, texH = 0;
        d3d11Bridge_->getTextureSize(texW, texH);
        if (nativeTex && texW > 0 && texH > 0) {
            QSize texSize(static_cast<int>(texW), static_cast<int>(texH));
            QSGTexture* wrappedTex = QNativeInterface::QSGD3D11Texture::fromNative(
                nativeTex, window(), texSize);
            if (wrappedTex) {
                auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
                if (!node) {
                    node = new QSGSimpleTextureNode();
                    node->setOwnsTexture(true);
                }
                node->setTexture(wrappedTex);
                node->setRect(computeTargetRect(boundingRect(), texW, texH));
                node->setFiltering(QSGTexture::Linear);
                return node;
            }
        }
    }
#endif

    auto* node = static_cast<QSGSimpleTextureNode*>(oldNode);
    if (!node && !frameReady_.load() && lastFrameImage_.isNull()) {
        return nullptr;
    }
    if (!node) {
        node = new QSGSimpleTextureNode();
        node->setOwnsTexture(true);
    }

    if (frameReady_.load()) {
        std::vector<uint8_t> frameData;
        uint32_t w = 0, h = 0;

        {
            std::lock_guard<std::mutex> lock(frameMutex_);
            if (frameReady_ && !pendingFrameData_.empty()) {
                frameData = std::move(pendingFrameData_);
                pendingFrameData_.clear();
                w = pendingWidth_;
                h = pendingHeight_;
                frameReady_ = false;
            }
        }

        if (!frameData.empty() && w > 0 && h > 0) {
            QImage image(frameData.data(), static_cast<int>(w), static_cast<int>(h),
                         static_cast<int>(w) * 4, QImage::Format_RGBA8888);
            QImage copy = image.copy();
            lastFrameImage_ = copy;
            lastFrameWidth_ = w;
            lastFrameHeight_ = h;

            QSGTexture* texture = window()->createTextureFromImage(
                copy, QQuickWindow::TextureIsOpaque);
            if (texture) {
                node->setTexture(texture);
                QRectF target = computeTargetRect(boundingRect(), w, h);
                node->setRect(target);
            }
        }
    } else if (!lastFrameImage_.isNull() && lastFrameWidth_ > 0 && lastFrameHeight_ > 0) {
        QSGTexture* texture = window()->createTextureFromImage(
            lastFrameImage_, QQuickWindow::TextureIsOpaque);
        if (texture) {
            node->setTexture(texture);
            node->setRect(computeTargetRect(boundingRect(), lastFrameWidth_, lastFrameHeight_));
        }
    } else if (lastFrameWidth_ > 0 && lastFrameHeight_ > 0) {
        node->setRect(computeTargetRect(boundingRect(), lastFrameWidth_, lastFrameHeight_));
    } else {
        node->setRect(boundingRect());
    }

    if (!node->texture()) {
        delete node;
        return nullptr;
    }

    node->setFiltering(QSGTexture::Linear);
    return node;
}

void VideoOutputItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        update();
    }
}

QRectF VideoOutputItem::computeTargetRect(const QRectF& bounds, uint32_t frameW, uint32_t frameH) {
    if (frameW == 0 || frameH == 0) return bounds;

    double frameAspect = static_cast<double>(frameW) / static_cast<double>(frameH);
    double boundsAspect = bounds.width() / bounds.height();

    double targetW, targetH;
    if (frameAspect > boundsAspect) {
        targetW = bounds.width();
        targetH = bounds.width() / frameAspect;
    } else {
        targetH = bounds.height();
        targetW = bounds.height() * frameAspect;
    }

    double x = bounds.x() + (bounds.width() - targetW) / 2.0;
    double y = bounds.y() + (bounds.height() - targetH) / 2.0;

    return QRectF(x, y, targetW, targetH);
}

} // namespace qml
} // namespace hlplayer
