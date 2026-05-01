#ifndef HLPLAYER_CPUFALLBACKDECODER_H
#define HLPLAYER_CPUFALLBACKDECODER_H

#include <hlplayer/HWDecoder.h>
#include <vector>
#include <cstdint>
#include <memory>

namespace hlplayer {

class HLPLAYER_CORE_API CPUFallbackDecoder final : public IHWDecoder {
public:
    CPUFallbackDecoder();
    ~CPUFallbackDecoder() override;

    CPUFallbackDecoder(const CPUFallbackDecoder&) = delete;
    CPUFallbackDecoder& operator=(const CPUFallbackDecoder&) = delete;
    CPUFallbackDecoder(CPUFallbackDecoder&&) noexcept;
    CPUFallbackDecoder& operator=(CPUFallbackDecoder&&) noexcept;

    Result<void> open(const DecoderConfig& config) override;
    Result<GpuFrame> decode(const uint8_t* data, size_t size, double pts) override;
    Result<std::vector<GpuFrame>> flush() override;
    void close() override;

    DecodeBackend getBackend() const override;
    bool supportsCodec(Codec codec) const override;

private:
    DecoderConfig config_;
    std::vector<uint8_t> frameBuffer_;
    bool isOpen_ = false;
};

} // namespace hlplayer

#endif // HLPLAYER_CPUFALLBACKDECODER_H
