#ifndef HLPLAYER_NCNNSUPERRESOLUTION_H
#define HLPLAYER_NCNNSUPERRESOLUTION_H

#include <hlplayer/IPipelineNode.h>
#include <hlplayer/IVRAMBudgetManager.h>
#include <hlplayer/GpuFrameContract.h>

#include <memory>
#include <mutex>
#include <string>

namespace ncnn {
class Net;
class Mat;
} // namespace ncnn

namespace hlplayer {

struct NcnnSRConfig {
    std::string modelPath;
    int scaleFactor = 2;
    int vulkanDeviceIndex = -1;
    std::shared_ptr<IVRAMBudgetManager> vramBudgetManager;
};

class NcnnSuperResolution : public IPipelineNode {
public:
    explicit NcnnSuperResolution(NcnnSRConfig config);
    ~NcnnSuperResolution() override;

    // Non-copyable, non-movable (owns ncnn::Net + VkCompute)
    NcnnSuperResolution(const NcnnSuperResolution&) = delete;
    NcnnSuperResolution& operator=(const NcnnSuperResolution&) = delete;
    NcnnSuperResolution(NcnnSuperResolution&&) = delete;
    NcnnSuperResolution& operator=(NcnnSuperResolution&&) = delete;

    // ---- IPipelineNode interface ----

    Result<void> initialize() override;
    Result<GpuFrame> process(const GpuFrame& frame) override;
    Result<std::vector<GpuFrame>> flush() override;
    void reset() override;
    std::string nodeName() const override;
    bool isHealthy() const override;

private:
    /// Validate the scale factor (must be 2, 3, or 4).
    static bool isValidScaleFactor(int scale);

    /// Estimate VRAM required for the model based on scale factor and
    /// a typical input resolution.
    uint64_t estimateModelVRAM() const;

    /// Convert RGBA8 cpuData from GpuFrame into an ncnn::Mat (CHW float).
    /// Returns nullptr on failure (e.g., empty cpuData).
    std::unique_ptr<ncnn::Mat> frameToNcnnMat(const GpuFrame& frame) const;

    /// Convert ncnn::Mat output back to RGBA8 cpuData vector.
    std::vector<uint8_t> ncnnMatToRGBA(const ncnn::Mat& mat, uint32_t outWidth, uint32_t outHeight) const;

    // Configuration
    NcnnSRConfig config_;

    // NCNN resources (raw pointer – we manage lifetime manually)
    ncnn::Net* net_ = nullptr;

    // Synchronization
    mutable std::mutex inferenceMutex_;

    // Health tracking
    bool initialized_ = false;
    bool healthy_ = true;

    // VRAM tracking
    uint64_t modelVRAMBytes_ = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_NCNNSUPERRESOLUTION_H
