#ifndef HLPLAYER_RESUMEMANAGER_H
#define HLPLAYER_RESUMEMANAGER_H

#include <hlplayer/Export.h>
#include <hlplayer/ICheckpointManager.h>
#include <hlplayer/Result.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace hlplayer {

/**
 * @brief Resume manager for offline transcoding pipeline
 *
 * Orchestrates checkpoint save/restore workflow and manages temporary output files.
 * Detects incomplete processing on startup and supports resuming from checkpoint.
 * All operations are thread-safe for concurrent processing.
 */
class HLPLAYER_CORE_API ResumeManager {
public:
    /**
     * @brief Construct resume manager with checkpoint manager
     *
     * @param checkpointManager Checkpoint manager for persistence
     */
    explicit ResumeManager(std::shared_ptr<ICheckpointManager> checkpointManager);

    /**
     * @brief Destructor
     */
    ~ResumeManager() = default;

    // Non-copyable, movable
    ResumeManager(const ResumeManager&) = delete;
    ResumeManager& operator=(const ResumeManager&) = delete;
    ResumeManager(ResumeManager&&) noexcept = default;
    ResumeManager& operator=(ResumeManager&&) noexcept = default;

    /**
     * @brief Save current processing progress
     *
     * Creates CheckpointInfo and saves via ICheckpointManager.
     * Should be called periodically during processing (e.g., every 100 frames).
     *
     * @param sourcePath Source video file path
     * @param outputPath Output video file path
     * @param frameNum Last successfully processed frame number
     * @param totalFrames Total number of frames in source
     * @param pipelineConfig Pipeline configuration (JSON string)
     * @return Result<void> Success or error information
     */
    Result<void> saveProgress(
        const std::string& sourcePath,
        const std::string& outputPath,
        uint64_t frameNum,
        uint64_t totalFrames,
        const std::string& pipelineConfig
    );

    /**
     * @brief Check if a job can be resumed
     *
     * @param sourcePath Source video file path to check
     * @return Result<bool> True if job can be resumed, false otherwise
     */
    Result<bool> checkForResumable(const std::string& sourcePath);

    /**
     * @brief Get checkpoint info for a specific source
     *
     * @param sourcePath Source video file path
     * @return Result<CheckpointInfo> Checkpoint information
     */
    Result<CheckpointInfo> getCheckpointInfo(const std::string& sourcePath);

    /**
     * @brief Resume from checkpoint
     *
     * Loads checkpoint and returns frame number to resume from.
     * Validates that source and output paths haven't changed.
     *
     * @param sourcePath Source video file path
     * @return Result<uint64_t> Frame number to resume from
     */
    Result<uint64_t> resumeFromCheckpoint(const std::string& sourcePath);

    /**
     * @brief Complete processing
     *
     * Marks processing as done and cleans up checkpoint and temp files.
     * Renames temporary output file to final output path.
     *
     * @param sourcePath Source video file path
     * @return Result<void> Success or error information
     */
    Result<void> completeProcessing(const std::string& sourcePath);

    /**
     * @brief List all resumable jobs
     *
     * Scans checkpoint directory for all incomplete jobs.
     *
     * @return Result<std::vector<CheckpointInfo>> List of checkpoint information
     */
    Result<std::vector<CheckpointInfo>> listResumableJobs();

    /**
     * @brief Cancel processing
     *
     * Cleans up checkpoint and temporary files for a cancelled job.
     *
     * @param sourcePath Source video file path
     * @return Result<void> Success or error information
     */
    Result<void> cancelProcessing(const std::string& sourcePath);

    /**
     * @brief Get temporary output file path
     *
     * Returns the path to the temporary output file during processing.
     *
     * @param outputPath Final output file path
     * @return std::filesystem::path Temporary output file path
     */
    static std::filesystem::path getTempOutputPath(const std::string& outputPath);

    /**
     * @brief Finalize output file
     *
     * Renames temporary output file to final output path.
     *
     * @param outputPath Final output file path
     * @return Result<void> Success or error information
     */
    Result<void> finalizeOutput(const std::string& outputPath);

    /**
     * @brief Clean all checkpoints and temp files
     *
     * @return Result<void> Success or error information
     */
    Result<void> cleanAll();

private:
    std::shared_ptr<ICheckpointManager> checkpointManager_;
    mutable std::mutex mutex_;
    static constexpr const char* TEMP_FILE_SUFFIX = ".part";
};

} // namespace hlplayer

#endif // HLPLAYER_RESUMEMANAGER_H
