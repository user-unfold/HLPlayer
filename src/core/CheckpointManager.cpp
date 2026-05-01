#include "CheckpointManager.h"
#include <spdlog/spdlog.h>
#include <fstream>
#include <chrono>
#include <functional>

namespace hlplayer {

CheckpointManager::CheckpointManager() {
    tempDir_ = std::filesystem::temp_directory_path() / "hlplayer_checkpoints";
    std::error_code ec;
    if (!std::filesystem::exists(tempDir_, ec)) {
        if (ec) {
            spdlog::error("Failed to check temp directory existence: {}", ec.message());
            return;
        }
        std::filesystem::create_directories(tempDir_, ec);
        if (ec) {
            spdlog::error("Failed to create temp directory {}: {}", tempDir_.string(), ec.message());
        } else {
            spdlog::info("Created checkpoint directory: {}", tempDir_.string());
        }
    } else {
        spdlog::info("Checkpoint directory exists: {}", tempDir_.string());
    }
}

CheckpointManager::CheckpointManager(const std::filesystem::path& tempDir)
    : tempDir_(tempDir) {
    std::error_code ec;
    if (!std::filesystem::exists(tempDir_, ec)) {
        if (ec) {
            spdlog::error("Failed to check temp directory existence: {}", ec.message());
            return;
        }
        std::filesystem::create_directories(tempDir_, ec);
        if (ec) {
            spdlog::error("Failed to create temp directory {}: {}", tempDir_.string(), ec.message());
        } else {
            spdlog::info("Created checkpoint directory: {}", tempDir_.string());
        }
    } else {
        spdlog::info("Checkpoint directory exists: {}", tempDir_.string());
    }
}

CheckpointManager::~CheckpointManager() = default;

Result<void> CheckpointManager::saveCheckpoint(const CheckpointInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        auto checkpointPath = getCheckpointPath(info.sourcePath);
        auto jsonData = serializeToJson(info);

        std::ofstream file(checkpointPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            spdlog::error("Failed to open checkpoint file for writing: {}", checkpointPath.string());
            return Result<void>::error(PlayerError::InvalidState);
        }

        file << jsonData.dump(4); // Pretty print with 4-space indentation
        file.close();

        if (!file.good()) {
            spdlog::error("Failed to write checkpoint file: {}", checkpointPath.string());
            return Result<void>::error(PlayerError::InvalidState);
        }

        spdlog::info("Checkpoint saved successfully for source: {}, frame: {}/{}",
                     info.sourcePath, info.lastProcessedFrame, info.totalFrames);
        return Result<void>::success();

    } catch (const std::exception& e) {
        spdlog::error("Exception while saving checkpoint: {}", e.what());
        return Result<void>::error(PlayerError::InvalidState);
    }
}

Result<CheckpointInfo> CheckpointManager::restoreCheckpoint(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        auto checkpointPath = getCheckpointPath(sourcePath);

        if (!std::filesystem::exists(checkpointPath)) {
            spdlog::warn("Checkpoint not found for source: {}", sourcePath);
            return Result<CheckpointInfo>::error(PlayerError::InvalidState);
        }

        std::ifstream file(checkpointPath);
        if (!file.is_open()) {
            spdlog::error("Failed to open checkpoint file for reading: {}", checkpointPath.string());
            return Result<CheckpointInfo>::error(PlayerError::InvalidState);
        }

        nlohmann::json jsonData;
        file >> jsonData;
        file.close();

        auto info = deserializeFromJson(jsonData);
        spdlog::info("Checkpoint restored successfully for source: {}, frame: {}/{}",
                     sourcePath, info.lastProcessedFrame, info.totalFrames);
        return Result<CheckpointInfo>::success(info);

    } catch (const std::exception& e) {
        spdlog::error("Exception while restoring checkpoint: {}", e.what());
        return Result<CheckpointInfo>::error(PlayerError::InvalidState);
    }
}

Result<void> CheckpointManager::cleanCheckpoint(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        auto checkpointPath = getCheckpointPath(sourcePath);

        if (!std::filesystem::exists(checkpointPath)) {
            spdlog::warn("Checkpoint file does not exist: {}", checkpointPath.string());
            return Result<void>::success();
        }

        std::error_code ec;
        std::filesystem::remove(checkpointPath, ec);
        if (ec) {
            spdlog::error("Failed to remove checkpoint file {}: {}", checkpointPath.string(), ec.message());
            return Result<void>::error(PlayerError::InvalidState);
        }

        spdlog::info("Checkpoint cleaned successfully for source: {}", sourcePath);
        return Result<void>::success();

    } catch (const std::exception& e) {
        spdlog::error("Exception while cleaning checkpoint: {}", e.what());
        return Result<void>::error(PlayerError::InvalidState);
    }
}

Result<bool> CheckpointManager::hasCheckpoint(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        auto checkpointPath = getCheckpointPath(sourcePath);
        bool exists = std::filesystem::exists(checkpointPath);
        return Result<bool>::success(exists);

    } catch (const std::exception& e) {
        spdlog::error("Exception while checking checkpoint existence: {}", e.what());
        return Result<bool>::error(PlayerError::InvalidState);
    }
}

Result<CheckpointInfo> CheckpointManager::getCheckpointInfo(const std::string& sourcePath) {
    return restoreCheckpoint(sourcePath);
}

Result<void> CheckpointManager::cleanAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        if (!std::filesystem::exists(tempDir_)) {
            spdlog::warn("Checkpoint directory does not exist: {}", tempDir_.string());
            return Result<void>::success();
        }

        std::error_code ec;
        std::uintmax_t removedCount = std::filesystem::remove_all(tempDir_, ec);
        if (ec) {
            spdlog::error("Failed to remove checkpoint directory {}: {}", tempDir_.string(), ec.message());
            return Result<void>::error(PlayerError::InvalidState);
        }

        // Recreate the directory for future checkpoints
        std::filesystem::create_directories(tempDir_, ec);
        if (ec) {
            spdlog::error("Failed to recreate checkpoint directory: {}", ec.message());
            return Result<void>::error(PlayerError::InvalidState);
        }

        spdlog::info("Cleaned all checkpoints, removed {} files/dirs", removedCount);
        return Result<void>::success();

    } catch (const std::exception& e) {
        spdlog::error("Exception while cleaning all checkpoints: {}", e.what());
        return Result<void>::error(PlayerError::InvalidState);
    }
}

std::string CheckpointManager::generateHash(const std::string& sourcePath) const {
    std::hash<std::string> hasher;
    size_t hashValue = hasher(sourcePath);
    return std::to_string(hashValue);
}

std::filesystem::path CheckpointManager::getCheckpointPath(const std::string& sourcePath) const {
    std::string hash = generateHash(sourcePath);
    return tempDir_ / (hash + CHECKPOINT_FILE_SUFFIX);
}

nlohmann::json CheckpointManager::serializeToJson(const CheckpointInfo& info) const {
    nlohmann::json j;
    j["sourcePath"] = info.sourcePath;
    j["outputPath"] = info.outputPath;
    j["lastProcessedFrame"] = info.lastProcessedFrame;
    j["totalFrames"] = info.totalFrames;
    j["timestamp"] = info.timestamp;
    j["pipelineConfig"] = info.pipelineConfig;
    return j;
}

CheckpointInfo CheckpointManager::deserializeFromJson(const nlohmann::json& j) const {
    CheckpointInfo info;
    info.sourcePath = j["sourcePath"].get<std::string>();
    info.outputPath = j["outputPath"].get<std::string>();
    info.lastProcessedFrame = j["lastProcessedFrame"].get<uint64_t>();
    info.totalFrames = j["totalFrames"].get<uint64_t>();
    info.timestamp = j["timestamp"].get<uint64_t>();
    info.pipelineConfig = j["pipelineConfig"].get<std::string>();
    return info;
}

} // namespace hlplayer
