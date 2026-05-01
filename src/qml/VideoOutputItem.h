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
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QImage>

#include <VulkanVideoSink.h>

#ifdef _WIN32
#include <D3D11RenderBridge.h>
#endif

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace hlplayer {
namespace qml {

class HLPLAYER_QML_API VideoOutputItem : public QQuickItem {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(hlplayer::render::VulkanVideoSink* videoSink READ videoSink WRITE setVideoSink NOTIFY videoSinkChanged)

public:
    explicit VideoOutputItem(QQuickItem* parent = nullptr);
    ~VideoOutputItem() override;

    hlplayer::render::VulkanVideoSink* videoSink() const;
    void setVideoSink(hlplayer::render::VulkanVideoSink* sink);

#ifdef _WIN32
    void setD3D11Bridge(hlplayer::render::D3D11RenderBridge* bridge);
#endif

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;

signals:
    void videoSinkChanged();

private:
    void copyPendingFrame();
    static QRectF computeTargetRect(const QRectF& bounds, uint32_t frameW, uint32_t frameH);

    render::VulkanVideoSink* videoSink_ = nullptr;

#ifdef _WIN32
    render::D3D11RenderBridge* d3d11Bridge_ = nullptr;
#endif

    std::mutex frameMutex_;
    std::vector<uint8_t> pendingFrameData_;
    uint32_t pendingWidth_ = 0;
    uint32_t pendingHeight_ = 0;
    uint32_t lastFrameWidth_ = 0;
    uint32_t lastFrameHeight_ = 0;
    std::atomic<bool> frameReady_{false};
    QImage lastFrameImage_;
};

} // namespace qml
} // namespace hlplayer
