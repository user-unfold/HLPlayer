#ifndef HLPLAYER_CHECKPOINTMANAGER_H
#define HLPLAYER_CHECKPOINTMANAGER_H

#include <hlplayer/ICheckpointManager.h>
#include <hlplayer/Export.h>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <mutex>
#include <string>

namespace hlplayer {

/**
 * @brief Implementation of checkpoint manager for video processing
 *
 * Manages checkpoint storage in JSON format within a temporary directory.
 * All operations are thread-safe and designed for background thread execution.
 */
class HLPLAYER_CORE_API CheckpointManager : public ICheckpointManager {
public:
    /**
     * @brief Construct checkpoint manager with default temp directory
     *
     * Creates temp directory at: std::filesystem::temp_directory_path() / "hlplayer_checkpoints"
     */
    CheckpointManager();

    /**
     * @brief Construct checkpoint manager with custom directory
     *
     * @param tempDir Custom directory for checkpoint storage
     */
    explicit CheckpointManager(const std::filesystem::path& tempDir);

    /**
     * @brief Destructor - cleans up resources
     */
    ~CheckpointManager() override;

    CheckpointManager(const CheckpointManager&) = delete;
    CheckpointManager& operator=(const CheckpointManager&) = delete;
    CheckpointManager(CheckpointManager&&) noexcept = default;
    CheckpointManager& operator=(CheckpointManager&&) noexcept = default;

    Result<void> saveCheckpoint(const CheckpointInfo& info) override;
    Result<CheckpointInfo> restoreCheckpoint(const std::string& sourcePath) override;
    Result<void> cleanCheckpoint(const std::string& sourcePath) override;
    Result<bool> hasCheckpoint(const std::string& sourcePath) override;
    Result<CheckpointInfo> getCheckpointInfo(const std::string& sourcePath) override;

    /**
     * @brief Clean all checkpoint files from storage directory
     *
     * @return Result<void> Success or error information
     */
    Result<void> cleanAll();

private:
    std::string generateHash(const std::string& sourcePath) const;
    std::filesystem::path getCheckpointPath(const std::string& sourcePath) const;
    nlohmann::json serializeToJson(const CheckpointInfo& info) const;
    CheckpointInfo deserializeFromJson(const nlohmann::json& j) const;

    std::filesystem::path tempDir_;
    mutable std::mutex mutex_;
    static constexpr const char* CHECKPOINT_FILE_SUFFIX = ".checkpoint.json";
};

}

#endif
