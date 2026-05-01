#ifndef HLPLAYER_ICHECKPOINTMANAGER_H
#define HLPLAYER_ICHECKPOINTMANAGER_H

#include <hlplayer/Export.h>
#include <hlplayer/Result.h>
#include <cstdint>
#include <string>

namespace hlplayer {

/**
 * @brief Checkpoint information structure
 *
 * Contains all necessary data to restore processing state.
 * Checkpoints are saved every 100 frames during processing.
 */
struct CheckpointInfo {
    std::string sourcePath;       ///< Source video file path
    std::string outputPath;       ///< Output video file path
    uint64_t lastProcessedFrame;  ///< Last successfully processed frame number
    uint64_t totalFrames;         ///< Total number of frames in source
    uint64_t timestamp;            ///< Checkpoint creation timestamp (milliseconds since epoch)
    std::string pipelineConfig;   ///< Pipeline configuration (JSON string)
};

/**
 * @brief Checkpoint manager interface
 *
 * Manages checkpoint creation, restoration, and cleanup for video processing.
 * All save/restore operations must be thread-safe to support concurrent processing.
 */
class HLPLAYER_CORE_API ICheckpointManager {
public:
    virtual ~ICheckpointManager() = default;

    /**
     * @brief Save checkpoint data for current processing state
     *
     * @param info Checkpoint information to save
     * @return Result<void> Success or error information
     *
     * @note This method must be thread-safe for concurrent calls
     */
    virtual Result<void> saveCheckpoint(const CheckpointInfo& info) = 0;

    /**
     * @brief Restore checkpoint data for a specific source
     *
     * @param sourcePath Source video file path
     * @return Result<CheckpointInfo> Retrieved checkpoint information
     *
     * @note This method must be thread-safe for concurrent calls
     */
    virtual Result<CheckpointInfo> restoreCheckpoint(const std::string& sourcePath) = 0;

    /**
     * @brief Remove checkpoint data for a specific source
     *
     * @param sourcePath Source video file path to clean up
     * @return Result<void> Success or error information
     *
     * @note This method must be thread-safe for concurrent calls
     */
    virtual Result<void> cleanCheckpoint(const std::string& sourcePath) = 0;

    /**
     * @brief Check if checkpoint exists for a specific source
     *
     * @param sourcePath Source video file path to check
     * @return Result<bool> True if checkpoint exists, false otherwise
     *
     * @note This method must be thread-safe for concurrent calls
     */
    virtual Result<bool> hasCheckpoint(const std::string& sourcePath) = 0;

    /**
     * @brief Get checkpoint information for a specific source
     *
     * @param sourcePath Source video file path to query
     * @return Result<CheckpointInfo> Checkpoint information
     *
     * @note This method must be thread-safe for concurrent calls
     */
    virtual Result<CheckpointInfo> getCheckpointInfo(const std::string& sourcePath) = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_ICHECKPOINTMANAGER_H
