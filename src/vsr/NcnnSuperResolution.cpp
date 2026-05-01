#include <hlplayer/NcnnSuperResolution.h>

#include "net.h"
#include "mat.h"

#include <algorithm>
#include <string>

namespace hlplayer {

NcnnSuperResolution::NcnnSuperResolution(NcnnSRConfig config)
    : config_(std::move(config)) {}

NcnnSuperResolution::~NcnnSuperResolution() {
    reset();

    if (net_) {
        net_->clear();
        delete net_;
        net_ = nullptr;
    }
}

Result<void> NcnnSuperResolution::initialize() {
    if (initialized_) {
        return Result<void>::error(PlayerError::InvalidState);
    }

    if (!isValidScaleFactor(config_.scaleFactor)) {
        return Result<void>::error(PlayerError::UnsupportedFormat);
    }

    if (config_.modelPath.empty()) {
        return Result<void>::error(PlayerError::InvalidURL);
    }

    net_ = new ncnn::Net();

    const std::string paramPath = config_.modelPath + ".param";
    const std::string binPath   = config_.modelPath + ".bin";

    int ret = net_->load_param(paramPath.c_str());
    if (ret != 0) {
        net_->clear();
        delete net_;
        net_ = nullptr;
        return Result<void>::error(PlayerError::DecodeError);
    }

    ret = net_->load_model(binPath.c_str());
    if (ret != 0) {
        net_->clear();
        delete net_;
        net_ = nullptr;
        return Result<void>::error(PlayerError::DecodeError);
    }

    modelVRAMBytes_ = estimateModelVRAM();
    if (config_.vramBudgetManager) {
        auto allocResult = config_.vramBudgetManager->requestAllocation(modelVRAMBytes_);
        if (allocResult.hasError()) {
            healthy_ = false;
        }
    }

    initialized_ = true;
    healthy_ = true;
    return Result<void>::success();
}

Result<GpuFrame> NcnnSuperResolution::process(const GpuFrame& frame) {
    if (!initialized_ || !healthy_) {
        return Result<GpuFrame>::error(PlayerError::InvalidState);
    }

    if (frame.width == 0 || frame.height == 0) {
        return Result<GpuFrame>::error(PlayerError::UnsupportedFormat);
    }

    if (frame.deviceLost) {
        return Result<GpuFrame>::success(frame);
    }

    std::lock_guard<std::mutex> lock(inferenceMutex_);

    auto inputMat = frameToNcnnMat(frame);
    if (!inputMat) {
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    const int scaleFactor = config_.scaleFactor;
    const uint32_t outWidth  = frame.width  * static_cast<uint32_t>(scaleFactor);
    const uint32_t outHeight = frame.height * static_cast<uint32_t>(scaleFactor);

    ncnn::Mat outputMat;
    {
        ncnn::Extractor ex = net_->create_extractor();
        ex.set_light_mode(true);

        ex.input("input", *inputMat);

        int ret = ex.extract("output", outputMat);
        if (ret != 0) {
            healthy_ = false;
            return Result<GpuFrame>::error(PlayerError::DecodeError);
        }
    }

    GpuFrame outFrame;
    outFrame.format     = PixelFormat::RGBA8;
    outFrame.width      = outWidth;
    outFrame.height     = outHeight;
    outFrame.colorSpace = frame.colorSpace;
    outFrame.colorRange = frame.colorRange;
    outFrame.timestamp  = frame.timestamp;
    outFrame.seekSerial = frame.seekSerial;

    outFrame.cpuData = ncnnMatToRGBA(outputMat, outWidth, outHeight);

    if (outFrame.cpuData.empty()) {
        healthy_ = false;
        return Result<GpuFrame>::error(PlayerError::DecodeError);
    }

    return Result<GpuFrame>::success(std::move(outFrame));
}

Result<std::vector<GpuFrame>> NcnnSuperResolution::flush() {
    return Result<std::vector<GpuFrame>>::success({});
}

void NcnnSuperResolution::reset() {
    if (config_.vramBudgetManager && modelVRAMBytes_ > 0) {
        config_.vramBudgetManager->release(modelVRAMBytes_);
        modelVRAMBytes_ = 0;
    }
    healthy_ = true;
}

std::string NcnnSuperResolution::nodeName() const {
    return "NcnnSuperResolution_" + std::to_string(config_.scaleFactor) + "x";
}

bool NcnnSuperResolution::isHealthy() const {
    return initialized_ && healthy_;
}

bool NcnnSuperResolution::isValidScaleFactor(int scale) {
    return scale == 2 || scale == 3 || scale == 4;
}

uint64_t NcnnSuperResolution::estimateModelVRAM() const {
    constexpr uint64_t baseModelBytes = 16ULL * 1024ULL * 1024ULL;
    constexpr uint64_t perScaleMB     = 8ULL  * 1024ULL * 1024ULL;
    return baseModelBytes + static_cast<uint64_t>(config_.scaleFactor) * perScaleMB;
}

std::unique_ptr<ncnn::Mat> NcnnSuperResolution::frameToNcnnMat(const GpuFrame& frame) const {
    if (frame.cpuData.empty()) {
        return nullptr;
    }

    const int w = static_cast<int>(frame.width);
    const int h = static_cast<int>(frame.height);
    const size_t pixelCount = static_cast<size_t>(w) * static_cast<size_t>(h);

    auto mat = std::make_unique<ncnn::Mat>(w, h, 3);

    float* rChannel = mat->channel(0);
    float* gChannel = mat->channel(1);
    float* bChannel = mat->channel(2);

    const uint8_t* src = frame.cpuData.data();
    for (size_t i = 0; i < pixelCount; ++i) {
        rChannel[i] = static_cast<float>(src[i * 4 + 0]) / 255.0f;
        gChannel[i] = static_cast<float>(src[i * 4 + 1]) / 255.0f;
        bChannel[i] = static_cast<float>(src[i * 4 + 2]) / 255.0f;
    }

    return mat;
}

std::vector<uint8_t> NcnnSuperResolution::ncnnMatToRGBA(
    const ncnn::Mat& mat, uint32_t outWidth, uint32_t outHeight) const {

    const size_t pixelCount = static_cast<size_t>(outWidth) * static_cast<size_t>(outHeight);
    std::vector<uint8_t> rgba(pixelCount * 4);

    if (mat.c < 3 || mat.w != static_cast<int>(outWidth) || mat.h != static_cast<int>(outHeight)) {
        return {};
    }

    const float* rChannel = mat.channel(0);
    const float* gChannel = mat.channel(1);
    const float* bChannel = mat.channel(2);

    uint8_t* dst = rgba.data();
    for (size_t i = 0; i < pixelCount; ++i) {
        dst[i * 4 + 0] = static_cast<uint8_t>(std::clamp(rChannel[i] * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 1] = static_cast<uint8_t>(std::clamp(gChannel[i] * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 2] = static_cast<uint8_t>(std::clamp(bChannel[i] * 255.0f, 0.0f, 255.0f));
        dst[i * 4 + 3] = 255;
    }

    return rgba;
}

} // namespace hlplayer
