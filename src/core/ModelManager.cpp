#include "ModelManager.h"

#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <regex>
#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#endif

namespace hlplayer {

ModelManager::ModelManager() = default;

ModelManager::~ModelManager() = default;

Result<void> ModelManager::scanDirectory(const std::string& dirPath) {
    std::error_code ec;
    std::filesystem::path dir(dirPath);

    if (!std::filesystem::exists(dir, ec)) {
        spdlog::error("Model directory does not exist: {}", dirPath);
        return Result<void>::error(PlayerError::InvalidURL);
    }

    if (!std::filesystem::is_directory(dir, ec)) {
        spdlog::error("Path is not a directory: {}", dirPath);
        return Result<void>::error(PlayerError::InvalidURL);
    }

    spdlog::info("Scanning model directory: {}", dirPath);

    {
        std::unique_lock<std::shared_mutex> lock(modelsMutex_);
        models_.clear();
        loadedModels_.clear();
    }

    scanNcnnModels(dir);
    scanOnnxModels(dir);

    spdlog::info("Scan complete. Found {} models.", models_.size());

    return Result<void>::success();
}

std::vector<ModelInfo> ModelManager::getAvailableModels() const {
    std::shared_lock<std::shared_mutex> lock(modelsMutex_);

    std::vector<ModelInfo> result;
    result.reserve(models_.size());

    for (const auto& [id, info] : models_) {
        result.push_back(info);
    }

    return result;
}

Result<void> ModelManager::loadModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(loadUnloadMutex_);

    {
        std::shared_lock<std::shared_mutex> modelsLock(modelsMutex_);

        if (models_.find(modelId) == models_.end()) {
            spdlog::error("Model not found: {}", modelId);
            return Result<void>::error(PlayerError::InvalidState);
        }

        if (loadedModels_.find(modelId) != loadedModels_.end()) {
            spdlog::warn("Model already loaded: {}", modelId);
            return Result<void>::error(PlayerError::InvalidState);
        }
    }

    {
        std::shared_lock<std::shared_mutex> modelsLock(modelsMutex_);
        const auto& info = models_[modelId];

        if (info.format == ModelFormat::NCNN) {
            std::filesystem::path paramPath(info.path);
            std::filesystem::path binPath = paramPath.parent_path() / (paramPath.stem().string() + ".bin");

            if (!std::filesystem::exists(paramPath)) {
                spdlog::error("NCNN .param file not found: {}", paramPath.string());
                return Result<void>::error(PlayerError::InvalidState);
            }

            if (!std::filesystem::exists(binPath)) {
                spdlog::error("NCNN .bin file not found: {}", binPath.string());
                return Result<void>::error(PlayerError::InvalidState);
            }
        } else if (info.format == ModelFormat::ONNX) {
            if (!std::filesystem::exists(info.path)) {
                spdlog::error("ONNX model file not found: {}", info.path);
                return Result<void>::error(PlayerError::InvalidState);
            }
        }
    }

    {
        std::unique_lock<std::shared_mutex> modelsLock(modelsMutex_);
        loadedModels_.insert(modelId);
    }

    spdlog::info("Model loaded: {}", modelId);

    return Result<void>::success();
}

Result<void> ModelManager::unloadModel(const std::string& modelId) {
    std::lock_guard<std::mutex> lock(loadUnloadMutex_);

    std::shared_lock<std::shared_mutex> modelsLock(modelsMutex_);

    if (loadedModels_.find(modelId) == loadedModels_.end()) {
        spdlog::warn("Model not loaded: {}", modelId);
        return Result<void>::error(PlayerError::InvalidState);
    }

    modelsLock.unlock();

    std::unique_lock<std::shared_mutex> modelsWriteLock(modelsMutex_);
    loadedModels_.erase(modelId);

    spdlog::info("Model unloaded: {}", modelId);

    return Result<void>::success();
}

Result<ModelInfo> ModelManager::getModelInfo(const std::string& modelId) const {
    std::shared_lock<std::shared_mutex> lock(modelsMutex_);

    auto it = models_.find(modelId);
    if (it == models_.end()) {
        return Result<ModelInfo>::error(PlayerError::InvalidState);
    }

    return Result<ModelInfo>::success(it->second);
}

bool ModelManager::isModelLoaded(const std::string& modelId) const {
    std::shared_lock<std::shared_mutex> lock(modelsMutex_);

    return loadedModels_.find(modelId) != loadedModels_.end();
}

bool ModelManager::parseNcnnParamFile(const std::filesystem::path& paramPath,
                                       float& outScaleFactor,
                                       uint32_t& outInputWidth,
                                       uint32_t& outInputHeight) const {
    std::ifstream file(paramPath);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    outScaleFactor = 0.0f;
    outInputWidth = 0;
    outInputHeight = 0;

    std::regex inputPattern(R"(Input\s+(\d+)\s+(\d+)\s+(\d+))");

    while (std::getline(file, line)) {
        std::smatch match;
        if (std::regex_search(line, match, inputPattern) && match.size() >= 4) {
            outInputWidth = std::stoul(match[1]);
            outInputHeight = std::stoul(match[2]);
            break;
        }
    }

    if (outScaleFactor == 0.0f) {
        outScaleFactor = extractScaleFromFilename(paramPath.filename().string());
    }

    if (outScaleFactor == 0.0f) {
        outScaleFactor = 2.0f;
    }

    if (outInputWidth == 0 || outInputHeight == 0) {
        outInputWidth = 64;
        outInputHeight = 64;
    }

    return true;
}

float ModelManager::extractScaleFromFilename(const std::string& filename) const {
    std::regex pattern(R"(_?(\d+)x)");
    std::smatch match;

    if (std::regex_search(filename, match, pattern) && match.size() >= 2) {
        try {
            return static_cast<float>(std::stoul(match[1]));
        } catch (...) {
        }
    }

    return 0.0f;
}

uint64_t ModelManager::estimateVRAMUsage(const std::filesystem::path& modelPath, float scaleFactor) const {
    std::error_code ec;
    uint64_t fileSize = std::filesystem::file_size(modelPath, ec);

    if (ec) {
        return 0;
    }

    float multiplier = scaleFactor > 0.0f ? scaleFactor : 2.0f;

    return static_cast<uint64_t>(fileSize * multiplier * 1.5f);
}

void ModelManager::scanNcnnModels(const std::filesystem::path& dirPath) {
    std::error_code ec;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath, ec)) {
        if (ec) {
            spdlog::warn("Error scanning directory: {}", ec.message());
            continue;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }

        if (entry.path().extension() != ".param") {
            continue;
        }

        std::string filename = entry.path().filename().string();
        std::string modelId = entry.path().stem().string();

        std::filesystem::path binPath = entry.path().parent_path() / (modelId + ".bin");

        if (!std::filesystem::exists(binPath, ec)) {
            continue;
        }

        float scaleFactor = extractScaleFromFilename(filename);
        uint32_t inputWidth = 0;
        uint32_t inputHeight = 0;

        parseNcnnParamFile(entry.path(), scaleFactor, inputWidth, inputHeight);

        uint64_t vramEstimate = estimateVRAMUsage(binPath, scaleFactor);

        ModelInfo info;
        info.name = modelId;
        info.path = entry.path().string();
        info.scaleFactor = scaleFactor;
        info.format = ModelFormat::NCNN;
        info.vramEstimateBytes = vramEstimate;
        info.inputWidth = inputWidth;
        info.inputHeight = inputHeight;
        info.description = "NCNN model";

        {
            std::unique_lock<std::shared_mutex> lock(modelsMutex_);
            models_[modelId] = info;
        }

        spdlog::info("Found NCNN model: {} (scale: {}x, VRAM: {} MB)",
                     modelId, scaleFactor, vramEstimate / (1024 * 1024));
    }
}

void ModelManager::scanOnnxModels(const std::filesystem::path& dirPath) {
    std::error_code ec;

    for (const auto& entry : std::filesystem::recursive_directory_iterator(dirPath, ec)) {
        if (ec) {
            spdlog::warn("Error scanning directory: {}", ec.message());
            continue;
        }

        if (!entry.is_regular_file(ec)) {
            continue;
        }

        if (entry.path().extension() != ".onnx") {
            continue;
        }

        std::string filename = entry.path().filename().string();
        std::string modelId = entry.path().stem().string();

        float scaleFactor = extractScaleFromFilename(filename);
        if (scaleFactor == 0.0f) {
            scaleFactor = 2.0f;
        }

        uint64_t vramEstimate = estimateVRAMUsage(entry.path(), scaleFactor);

        ModelInfo info;
        info.name = modelId;
        info.path = entry.path().string();
        info.scaleFactor = scaleFactor;
        info.format = ModelFormat::ONNX;
        info.vramEstimateBytes = vramEstimate;
        info.inputWidth = 0;
        info.inputHeight = 0;
        info.description = "ONNX model";

        {
            std::unique_lock<std::shared_mutex> lock(modelsMutex_);
            models_[modelId] = info;
        }

        spdlog::info("Found ONNX model: {} (scale: {}x, VRAM: {} MB)",
                     modelId, scaleFactor, vramEstimate / (1024 * 1024));
    }
}

}