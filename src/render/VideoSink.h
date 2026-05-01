#ifndef HLPLAYER_VIDEOSINK_H
#define HLPLAYER_VIDEOSINK_H

#ifndef HLPLAYER_RENDER_API
# ifdef _WIN32
#   ifdef HLPLAYER_RENDER_EXPORTS
#     define HLPLAYER_RENDER_API __declspec(dllexport)
#   else
#     define HLPLAYER_RENDER_API __declspec(dllimport)
#   endif
# else
#   define HLPLAYER_RENDER_API
# endif
#endif

#include <hlplayer/IVideoFrameSink.h>
#include <memory>
#include <functional>

namespace hlplayer {
namespace render {

/// Interface for receiving decoded frames from the media engine.
/// Concrete implementations bridge GPU frames to rendering backends
/// (Vulkan, D3D11, etc.).
class HLPLAYER_RENDER_API IRenderBridge {
public:
    virtual ~IRenderBridge() = default;

    /// Called when a new decoded GPU frame is available.
    /// @param frame The decoded GPU frame (may be zero-copy handle).
    virtual void presentFrame(const GpuFrame& frame) = 0;

    /// Called when the video format changes (e.g., resolution, pixel format).
    /// @param format New video format.
    virtual void onFormatChange(VideoFormat format) = 0;

    /// Reset the render bridge state (e.g., after seek or stop).
    virtual void reset() = 0;
};

/// Adapts an IVideoFrameSink (core SDK interface) to delegate to an IRenderBridge.
/// This bridges the media engine's callback contract to the rendering layer.
class HLPLAYER_RENDER_API FrameSinkAdapter : public IVideoFrameSink {
public:
    /// Construct an adapter wrapping the given render bridge.
    /// @param bridge The render bridge to delegate frames to. Must outlive this adapter.
    explicit FrameSinkAdapter(std::shared_ptr<IRenderBridge> bridge);

    ~FrameSinkAdapter() override = default;

    void onFrame(const GpuFrame& frame) override;
    void onFormatChanged(VideoFormat format) override;
    void reset() override;

    /// Get the underlying render bridge.
    std::shared_ptr<IRenderBridge> getBridge() const { return bridge_; }

private:
    std::shared_ptr<IRenderBridge> bridge_;
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_VIDEOSINK_H
