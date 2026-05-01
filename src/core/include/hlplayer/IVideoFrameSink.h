#ifndef HLPLAYER_IVIDEOFRAMESINK_H
#define HLPLAYER_IVIDEOFRAMESINK_H

#include <hlplayer/GpuFrameContract.h>
#include <hlplayer/Export.h>

namespace hlplayer {

enum class VideoFormat : uint8_t {
    Unknown = 0,
    NV12,
    P010,
    RGBA8,
    RGBA16F
};

class HLPLAYER_CORE_API IVideoFrameSink {
public:
    virtual ~IVideoFrameSink() = default;

    virtual void onFrame(const GpuFrame& frame) = 0;
    virtual void onFormatChanged(VideoFormat format) = 0;
    virtual void reset() = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IVIDEOFRAMESINK_H
