#ifndef HLPLAYER_IMODELMANAGER_H
#define HLPLAYER_IMODELMANAGER_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>
#include <cstdint>
#include <string>
#include <vector>

namespace hlplayer {

// Supported formats: .param+.bin (NCNN Vulkan), .onnx
enum class ModelFormat {
    NCNN,
    ONNX
};

struct ModelInfo {
    std::string name;
    std::string path;
    float scaleFactor;
    ModelFormat format;
    uint64_t vramEstimateBytes;
    uint32_t inputWidth;
    uint32_t inputHeight;
    std::string description;
};

// Default directory: %APPDATA%/HLPlayer/models/
class HLPLAYER_CORE_API IModelManager {
public:
    virtual ~IModelManager() = default;

    virtual Result<void> scanDirectory(const std::string& dirPath) = 0;
    virtual std::vector<ModelInfo> getAvailableModels() const = 0;
    virtual Result<void> loadModel(const std::string& modelId) = 0;
    virtual Result<void> unloadModel(const std::string& modelId) = 0;
    virtual Result<ModelInfo> getModelInfo(const std::string& modelId) const = 0;
    virtual bool isModelLoaded(const std::string& modelId) const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_IMODELMANAGER_H
