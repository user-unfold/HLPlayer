#include <hlplayer/CPUFallbackDecoder.h>
#include <cstring>

namespace hlplayer {

CPUFallbackDecoder::CPUFallbackDecoder() = default;

CPUFallbackDecoder::~CPUFallbackDecoder() {
    close();
}

CPUFallbackDecoder::CPUFallbackDecoder(CPUFallbackDecoder&& other) noexcept
    : config_(std::move(other.config_))
    , frameBuffer_(std::move(other.frameBuffer_))
    , isOpen_(other.isOpen_)
{
    other.isOpen_ = false;
}

CPUFallbackDecoder& CPUFallbackDecoder::operator=(CPUFallbackDecoder&& other) noexcept {
    if (this != &other) {
        close();
        config_ = std::move(other.config_);
        frameBuffer_ = std::move(other.frameBuffer_);
        isOpen_ = other.isOpen_;
        other.isOpen_ = false;
    }
    return *this;
}

Result<void> CPUFallbackDecoder::open(const DecoderConfig& config) {
    if (config.width == 0 || config.height == 0) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    close();
    config_ = config;

    size_t nv12Size = static_cast<size_t>(config.width) * config.height * 3 / 2;
    frameBuffer_.resize(nv12Size);
    isOpen_ = true;

    return Result<void>::success();
}

Result<GpuFrame> CPUFallbackDecoder::decode(const uint8_t* data, size_t size, double pts) {
    if (!isOpen_) {
        return Result<GpuFrame>::error(PlayerError::InvalidState);
    }

    if (data == nullptr || size == 0) {
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    size_t copyLen = std::min(size, frameBuffer_.size());
    std::memcpy(frameBuffer_.data(), data, copyLen);
    if (copyLen < frameBuffer_.size()) {
        std::memset(frameBuffer_.data() + copyLen, 0, frameBuffer_.size() - copyLen);
    }

    GpuFrame frame;
    frame.width = config_.width;
    frame.height = config_.height;
    frame.format = config_.outputPixelFormat;
    frame.timestamp = pts;
    frame.handle.nativeHandle = frameBuffer_.data();

    return Result<GpuFrame>::success(frame);
}

Result<std::vector<GpuFrame>> CPUFallbackDecoder::flush() {
    return Result<std::vector<GpuFrame>>::success({});
}

void CPUFallbackDecoder::close() {
    if (isOpen_) {
        frameBuffer_.clear();
        frameBuffer_.shrink_to_fit();
        isOpen_ = false;
    }
}

DecodeBackend CPUFallbackDecoder::getBackend() const {
    return DecodeBackend::CPU;
}

bool CPUFallbackDecoder::supportsCodec(Codec codec) const {
    (void)codec;
    return true;
}

} // namespace hlplayer
