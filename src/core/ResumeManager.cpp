#include "ResumeManager.h"
#include <spdlog/spdlog.h>
#include <chrono>
#include <fstream>

namespace hlplayer {

ResumeManager::ResumeManager(std::shared_ptr<ICheckpointManager> checkpointManager)
    : checkpointManager_(std::move(checkpointManager)) {
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager is null");
    }
}

Result<void> ResumeManager::saveProgress(
    const std::string& sourcePath,
    const std::string& outputPath,
    uint64_t frameNum,
    uint64_t totalFrames,
    const std::string& pipelineConfig) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty() || outputPath.empty()) {
        spdlog::error("ResumeManager: Source path or output path is empty");
        return Result<void>::error(PlayerError::InvalidURL);
    }
    
    if (frameNum > totalFrames) {
        spdlog::error("ResumeManager: Frame number {} exceeds total frames {}", frameNum, totalFrames);
        return Result<void>::error(PlayerError::InvalidState);
    }
    
    auto existingResult = checkpointManager_->hasCheckpoint(sourcePath);
    if (existingResult.hasError()) {
        spdlog::error("ResumeManager: Failed to check existing checkpoint for {}: {}", 
            sourcePath, static_cast<int>(existingResult.error()));
        return Result<void>::error(existingResult.error());
    }
    
    if (existingResult.value()) {
        spdlog::warn("ResumeManager: Overwriting existing checkpoint for {}", sourcePath);
    }
    
    CheckpointInfo info;
    info.sourcePath = sourcePath;
    info.outputPath = outputPath;
    info.lastProcessedFrame = frameNum;
    info.totalFrames = totalFrames;
    info.timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    info.pipelineConfig = pipelineConfig;
    
    auto saveResult = checkpointManager_->saveCheckpoint(info);
    if (saveResult.hasError()) {
        spdlog::error("ResumeManager: Failed to save checkpoint for {}: {}", 
            sourcePath, static_cast<int>(saveResult.error()));
        return Result<void>::error(saveResult.error());
    }
    
    spdlog::info("ResumeManager: Saved checkpoint for {} at frame {}/{}", 
        sourcePath, frameNum, totalFrames);
    
    return Result<void>::success();
}

Result<bool> ResumeManager::checkForResumable(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<bool>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty()) {
        spdlog::error("ResumeManager: Source path is empty");
        return Result<bool>::error(PlayerError::InvalidURL);
    }
    
    auto hasResult = checkpointManager_->hasCheckpoint(sourcePath);
    if (hasResult.hasError()) {
        spdlog::error("ResumeManager: Failed to check checkpoint for {}: {}", 
            sourcePath, static_cast<int>(hasResult.error()));
        return Result<bool>::error(hasResult.error());
    }
    
    if (!hasResult.value()) {
        spdlog::debug("ResumeManager: No checkpoint found for {}", sourcePath);
        return Result<bool>::success(false);
    }
    
    auto infoResult = checkpointManager_->getCheckpointInfo(sourcePath);
    if (infoResult.hasError()) {
        spdlog::error("ResumeManager: Failed to get checkpoint info for {}: {}", 
            sourcePath, static_cast<int>(infoResult.error()));
        return Result<bool>::error(infoResult.error());
    }
    
    const auto& info = infoResult.value();
    
    std::filesystem::path tempPath = getTempOutputPath(info.outputPath);
    if (!std::filesystem::exists(tempPath)) {
        spdlog::warn("ResumeManager: Checkpoint exists but temp file missing for {}: {}", 
            sourcePath, tempPath.string());
        return Result<bool>::success(false);
    }
    
    spdlog::info("ResumeManager: Found resumable checkpoint for {} at frame {}/{}", 
        sourcePath, info.lastProcessedFrame, info.totalFrames);
    
    return Result<bool>::success(true);
}

Result<CheckpointInfo> ResumeManager::getCheckpointInfo(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<CheckpointInfo>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty()) {
        spdlog::error("ResumeManager: Source path is empty");
        return Result<CheckpointInfo>::error(PlayerError::InvalidURL);
    }
    
    auto result = checkpointManager_->getCheckpointInfo(sourcePath);
    if (result.hasError()) {
        spdlog::error("ResumeManager: Failed to get checkpoint info for {}: {}", 
            sourcePath, static_cast<int>(result.error()));
        return Result<CheckpointInfo>::error(result.error());
    }
    
    return result;
}

Result<uint64_t> ResumeManager::resumeFromCheckpoint(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<uint64_t>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty()) {
        spdlog::error("ResumeManager: Source path is empty");
        return Result<uint64_t>::error(PlayerError::InvalidURL);
    }
    
    auto infoResult = checkpointManager_->getCheckpointInfo(sourcePath);
    if (infoResult.hasError()) {
        spdlog::error("ResumeManager: Failed to restore checkpoint for {}: {}", 
            sourcePath, static_cast<int>(infoResult.error()));
        return Result<uint64_t>::error(infoResult.error());
    }
    
    const auto& info = infoResult.value();
    
    if (info.sourcePath.empty() || info.outputPath.empty()) {
        spdlog::error("ResumeManager: Checkpoint for {} has empty source or output path", sourcePath);
        checkpointManager_->cleanCheckpoint(sourcePath);
        return Result<uint64_t>::error(PlayerError::InvalidState);
    }
    
    if (info.sourcePath != sourcePath) {
        spdlog::error("ResumeManager: Checkpoint source path mismatch: expected {}, got {}", 
            sourcePath, info.sourcePath);
        return Result<uint64_t>::error(PlayerError::InvalidState);
    }
    
    std::filesystem::path tempPath = getTempOutputPath(info.outputPath);
    if (!std::filesystem::exists(tempPath)) {
        spdlog::error("ResumeManager: Temp file missing for checkpoint {}: {}", 
            sourcePath, tempPath.string());
        checkpointManager_->cleanCheckpoint(sourcePath);
        return Result<uint64_t>::error(PlayerError::InvalidState);
    }
    
    uint64_t resumeFrame = info.lastProcessedFrame + 1;
    
    if (info.lastProcessedFrame >= info.totalFrames) {
        spdlog::warn("ResumeManager: Checkpoint for {} indicates processing already complete at frame {}/{}", 
            sourcePath, info.lastProcessedFrame, info.totalFrames);
        return Result<uint64_t>::error(PlayerError::InvalidState);
    }
    
    spdlog::info("ResumeManager: Resuming from checkpoint {} at frame {}/{} (continuing from {})", 
        sourcePath, info.lastProcessedFrame, info.totalFrames, resumeFrame);
    
    return Result<uint64_t>::success(resumeFrame);
}

Result<void> ResumeManager::completeProcessing(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty()) {
        spdlog::error("ResumeManager: Source path is empty");
        return Result<void>::error(PlayerError::InvalidURL);
    }
    
    auto infoResult = checkpointManager_->getCheckpointInfo(sourcePath);
    if (infoResult.hasError()) {
        if (infoResult.error() == PlayerError::Unknown) {
            spdlog::debug("ResumeManager: No checkpoint to clean up for {}", sourcePath);
            return Result<void>::success();
        }
        spdlog::error("ResumeManager: Failed to get checkpoint info for {}: {}", 
            sourcePath, static_cast<int>(infoResult.error()));
        return Result<void>::error(infoResult.error());
    }
    
    const auto& info = infoResult.value();
    
    auto finalizeResult = finalizeOutput(info.outputPath);
    if (finalizeResult.hasError()) {
        spdlog::error("ResumeManager: Failed to finalize output for {}: {}", 
            sourcePath, static_cast<int>(finalizeResult.error()));
    }
    
    auto cleanResult = checkpointManager_->cleanCheckpoint(sourcePath);
    if (cleanResult.hasError()) {
        spdlog::error("ResumeManager: Failed to clean checkpoint for {}: {}", 
            sourcePath, static_cast<int>(cleanResult.error()));
        return Result<void>::error(cleanResult.error());
    }
    
    spdlog::info("ResumeManager: Completed processing for {}", sourcePath);
    
    return Result<void>::success();
}

Result<std::vector<CheckpointInfo>> ResumeManager::listResumableJobs() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<std::vector<CheckpointInfo>>::error(PlayerError::InvalidState);
    }
    
    spdlog::warn("ResumeManager: listResumableJobs() requires access to checkpoint directory - not implemented in ICheckpointManager interface");
    
    return Result<std::vector<CheckpointInfo>>::success(std::vector<CheckpointInfo>{});
}

Result<void> ResumeManager::cancelProcessing(const std::string& sourcePath) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }
    
    if (sourcePath.empty()) {
        spdlog::error("ResumeManager: Source path is empty");
        return Result<void>::error(PlayerError::InvalidURL);
    }
    
    auto infoResult = checkpointManager_->getCheckpointInfo(sourcePath);
    if (!infoResult.hasError()) {
        const auto& info = infoResult.value();
        
        std::filesystem::path tempPath = getTempOutputPath(info.outputPath);
        if (std::filesystem::exists(tempPath)) {
            std::error_code ec;
            std::filesystem::remove(tempPath, ec);
            if (ec) {
                spdlog::warn("ResumeManager: Failed to delete temp file {}: {}", 
                    tempPath.string(), ec.message());
            } else {
                spdlog::info("ResumeManager: Deleted temp file: {}", tempPath.string());
            }
        }
    }
    
    auto cleanResult = checkpointManager_->cleanCheckpoint(sourcePath);
    if (cleanResult.hasError()) {
        spdlog::error("ResumeManager: Failed to clean checkpoint for {}: {}", 
            sourcePath, static_cast<int>(cleanResult.error()));
        return Result<void>::error(cleanResult.error());
    }
    
    spdlog::info("ResumeManager: Cancelled processing for {}", sourcePath);
    
    return Result<void>::success();
}

std::filesystem::path ResumeManager::getTempOutputPath(const std::string& outputPath) {
    return std::filesystem::path(outputPath + TEMP_FILE_SUFFIX);
}

Result<void> ResumeManager::finalizeOutput(const std::string& outputPath) {
    if (outputPath.empty()) {
        spdlog::error("ResumeManager: Output path is empty");
        return Result<void>::error(PlayerError::InvalidURL);
    }
    
    std::filesystem::path tempPath = getTempOutputPath(outputPath);
    std::filesystem::path finalPath(outputPath);
    
    if (!std::filesystem::exists(tempPath)) {
        spdlog::warn("ResumeManager: Temp file does not exist: {}", tempPath.string());
        return Result<void>::error(PlayerError::Unknown);
    }
    
    if (std::filesystem::exists(finalPath)) {
        spdlog::warn("ResumeManager: Final output file already exists: {}", finalPath.string());
        std::error_code ec;
        std::filesystem::remove(finalPath, ec);
        if (ec) {
            spdlog::error("ResumeManager: Failed to remove existing output file {}: {}", 
                finalPath.string(), ec.message());
            return Result<void>::error(PlayerError::Unknown);
        }
    }
    
    std::error_code ec;
    std::filesystem::rename(tempPath, finalPath, ec);
    if (ec) {
        spdlog::error("ResumeManager: Failed to rename {} to {}: {}", 
            tempPath.string(), finalPath.string(), ec.message());
        return Result<void>::error(PlayerError::Unknown);
    }
    
    spdlog::info("ResumeManager: Finalized output: {} (from {})", 
        finalPath.string(), tempPath.string());
    
    return Result<void>::success();
}

Result<void> ResumeManager::cleanAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!checkpointManager_) {
        spdlog::error("ResumeManager: CheckpointManager not initialized");
        return Result<void>::error(PlayerError::InvalidState);
    }
    
    spdlog::warn("ResumeManager: cleanAll() requires CheckpointManager::cleanAll() - not available in ICheckpointManager interface");
    
    return Result<void>::error(PlayerError::InvalidState);
}

} // namespace hlplayer
