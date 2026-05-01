#include "VideoSink.h"
#include <spdlog/spdlog.h>

namespace hlplayer {
namespace render {

FrameSinkAdapter::FrameSinkAdapter(std::shared_ptr<IRenderBridge> bridge)
    : bridge_(std::move(bridge)) {
    if (!bridge_) {
        spdlog::warn("FrameSinkAdapter created with null bridge");
    }
}

void FrameSinkAdapter::onFrame(const GpuFrame& frame) {
    if (!bridge_) {
        spdlog::warn("FrameSinkAdapter::onFrame called with null bridge");
        return;
    }
    bridge_->presentFrame(frame);
}

void FrameSinkAdapter::onFormatChanged(VideoFormat format) {
    if (!bridge_) {
        return;
    }
    bridge_->onFormatChange(format);
}

void FrameSinkAdapter::reset() {
    if (!bridge_) {
        return;
    }
    bridge_->reset();
}

} // namespace render
} // namespace hlplayer
