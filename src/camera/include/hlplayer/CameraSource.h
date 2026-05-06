#ifndef HLPLAYER_CAMERASOURCE_H
#define HLPLAYER_CAMERASOURCE_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/Result.h>
#include <FFmpegRAII.h>

#include <atomic>
#include <string>
#include <vector>

namespace hlplayer {

using namespace hlplayer::ffmpeg;

class HLPLAYER_CAMERA_API CameraSource {
public:
    CameraSource();
    ~CameraSource();

    CameraSource(const CameraSource&) = delete;
    CameraSource& operator=(const CameraSource&) = delete;
    CameraSource(CameraSource&&) = delete;
    CameraSource& operator=(CameraSource&&) = delete;

    Result<std::vector<CameraDeviceInfo>> enumerateDevices();
    Result<void> open(const std::string& devicePath, int width, int height, int fps);
    Result<void> close();
    Result<void> readFrame();
    bool isOpen() const;
    bool isAborted() const { return abortFlag_.load(); }
    void abort();

    const AVFrame* getFrame() const;

private:
    static bool deviceRegistered_;

    std::atomic<bool> abortFlag_{false};
    AVFormatContextPtr formatCtx_;
    AVCodecContextPtr codecCtx_;
    AVFramePtr currentFrame_;
    int videoStreamIndex_ = -1;
    bool open_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_CAMERASOURCE_H
