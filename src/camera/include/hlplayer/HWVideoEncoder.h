#pragma once
#include <hlplayer/CameraExport.h>
#include <hlplayer/HWEncoderDetector.h>
#include <hlplayer/IVideoEncoder.h>
#include <hlplayer/Result.h>
#include <memory>
#include <vector>
struct AVCodecContext;
struct AVBufferRef;
struct AVFrame;
namespace hlplayer {
struct HWEncodeConfig {
    int width = 1280;
    int height = 720;
    int fps = 30;
    int bitrate = 4000000;
    int gopSize = 60;
    int maxBFrames = 0;
    EncoderInfo encoderInfo;
};
class HLPLAYER_CAMERA_API HWVideoEncoder {
public:
    HWVideoEncoder();
    ~HWVideoEncoder();
    HWVideoEncoder(const HWVideoEncoder&) = delete;
    HWVideoEncoder& operator=(const HWVideoEncoder&) = delete;
    Result<void> init(const HWEncodeConfig& config);
    Result<std::vector<EncodedPacket>> encode(const AVFrame* cpuFrame);
    Result<std::vector<EncodedPacket>> flush();
    const AVCodecContext* context() const;
    AVPixelFormat hwPixFmt() const;
    bool isHardware() const;
    Result<void> ensureExtradata();
private:
    Result<void> initHardwareContext();
    Result<void> initEncoder();
    HWEncodeConfig config_;
    AVCodecContext* encCtx_ = nullptr;
    AVBufferRef* hwDeviceCtx_ = nullptr;
    AVBufferRef* hwFramesCtx_ = nullptr;
    AVFrame* hwFrame_ = nullptr;
    int frameIndex_ = 0;
};
}
