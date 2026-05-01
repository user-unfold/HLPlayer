#ifndef HLPLAYER_MODELMANAGER_H
#define HLPLAYER_MODELMANAGER_H

#include <hlplayer/IModelManager.h>
#include <hlplayer/Export.h>

#include <filesystem>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace hlplayer {

/**
 * Implementation of IModelManager for managing NCNN and ONNX super-resolution models.
 *
 * Scans directories for model files, parses metadata, and manages model lifecycle.
 * Thread-safe implementation using shared_mutex for read-heavy operations.
 */
class HLPLAYER_CORE_API ModelManager : public IModelManager {
public:
    ModelManager();
    ~ModelManager() override;

    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    ModelManager(ModelManager&&) = delete;
    ModelManager& operator=(ModelManager&&) = delete;

    /**
     * Scan directory for model files (.param+.bin pairs for NCNN, .onnx for ONNX)
     * @param dirPath Path to directory containing model files
     * @return Result<void> Success or error
     */
    Result<void> scanDirectory(const std::string& dirPath) override;

    /**
     * Get list of all discovered models
     * @return Vector of ModelInfo for all discovered models
     */
    std::vector<ModelInfo> getAvailableModels() const override;

    /**
     * Load a model by ID (marks as loaded, actual loading is done by VSR pipeline)
     * @param modelId Model identifier (filename without extension)
     * @return Result<void> Success or error
     */
    Result<void> loadModel(const std::string& modelId) override;

    /**
     * Unload a model by ID (marks as unloaded, releases resources)
     * @param modelId Model identifier
     * @return Result<void> Success or error
     */
    Result<void> unloadModel(const std::string& modelId) override;

    /**
     * Get information about a specific model
     * @param modelId Model identifier
     * @return Result<ModelInfo> Model info or error if not found
     */
    Result<ModelInfo> getModelInfo(const std::string& modelId) const override;

    /**
     * Check if a model is currently loaded
     * @param modelId Model identifier
     * @return true if loaded, false otherwise
     */
    bool isModelLoaded(const std::string& modelId) const override;

private:
    bool parseNcnnParamFile(const std::filesystem::path& paramPath,
                           float& outScaleFactor,
                           uint32_t& outInputWidth,
                           uint32_t& outInputHeight) const;

    float extractScaleFromFilename(const std::string& filename) const;

    uint64_t estimateVRAMUsage(const std::filesystem::path& modelPath, float scaleFactor) const;

    void scanNcnnModels(const std::filesystem::path& dirPath);

    void scanOnnxModels(const std::filesystem::path& dirPath);

private:
    std::unordered_map<std::string, ModelInfo> models_;
    std::unordered_set<std::string> loadedModels_;
    mutable std::shared_mutex modelsMutex_;
    std::mutex loadUnloadMutex_;
};

} // namespace hlplayer

#endif // HLPLAYER_MODELMANAGER_H
