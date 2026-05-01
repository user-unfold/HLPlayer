#include <hlplayer/VSRErrorCatalog.h>

#include <cstdint>

namespace hlplayer {

namespace {

constexpr VSErrorMessage g_errorMessages[] = {
    { VSRError::None,
      "No error",
      "Operation completed successfully",
      "" },

    { VSRError::ModelNotFound,
      "NCNN model load failed: .param file not found at {path}",
      "Super-resolution model could not be loaded. Please check your model files.",
      "Verify model files in %APPDATA%/HLPlayer/models/" },

    { VSRError::ModelLoadFailed,
      "NCNN model load failed: initialization error for {path}",
      "Super-resolution model failed to initialize.",
      "Check model compatibility and file integrity in %APPDATA%/HLPlayer/models/" },

    { VSRError::ModelCorrupted,
      "NCNN model corrupted: invalid model file format at {path}",
      "Super-resolution model file is corrupted and cannot be used.",
      "Re-download or restore the model files from %APPDATA%/HLPlayer/models/" },

    { VSRError::InferenceFailed,
      "NCNN inference failed: {reason}",
      "Super-resolution processing encountered an error.",
      "Try reducing output resolution or check system resources" },

    { VSRError::InferenceTimeout,
      "NCNN inference timeout: processing exceeded {timeout}ms",
      "Super-resolution processing took too long.",
      "Try reducing output resolution or system load" },

    { VSRError::VRAMExceeded,
      "VRAM allocation failed: exceeded available GPU memory ({available}MB)",
      "Not enough video memory for super-resolution.",
      "Reduce output resolution or close other GPU applications" },

    { VSRError::VRAMAllocationFailed,
      "VRAM allocation failed: {reason}",
      "Failed to allocate video memory for super-resolution.",
      "Reduce output resolution or try CPU processing mode" },

    { VSRError::EncoderNotFound,
      "Video encoder not found: {codec}",
      "Required video encoder is not available on your system.",
      "Install the required encoder or use a different format" },

    { VSRError::EncoderInitFailed,
      "Video encoder initialization failed: {reason}",
      "Failed to initialize the video encoder.",
      "Check encoder configuration and available codecs" },

    { VSRError::EncodingFailed,
      "Video encoding failed: frame {frame}, error: {error}",
      "An error occurred during video encoding.",
      "Try a different output format or reduce resolution" },

    { VSRError::MuxerOpenFailed,
      "Muxer open failed: {format}, reason: {reason}",
      "Failed to create output file container.",
      "Check file path permissions and available disk space" },

    { VSRError::MuxerWriteFailed,
      "Muxer write failed: {reason}",
      "Failed to write data to output file.",
      "Check disk space and file permissions" },

    { VSRError::MuxerFinalizeFailed,
      "Muxer finalize failed: {reason}",
      "Failed to complete output file.",
      "Verify output file integrity and disk space" },

    { VSRError::CheckpointSaveFailed,
      "Checkpoint save failed: unable to write to {path}",
      "Failed to save processing progress.",
      "Check disk space and file permissions in output directory" },

    { VSRError::CheckpointRestoreFailed,
      "Checkpoint restore failed: unable to read from {path}",
      "Failed to restore processing progress.",
      "Verify checkpoint file exists and is not corrupted" },

    { VSRError::CheckpointCorrupted,
      "Checkpoint corrupted: invalid format at {path}",
      "Saved progress data is corrupted.",
      "Delete the checkpoint file and restart processing" },

    { VSRError::PipelineAlreadyRunning,
      "Pipeline already running: cannot start new pipeline while one is active",
      "A super-resolution task is already in progress.",
      "Wait for the current task to complete or cancel it first" },

    { VSRError::PipelineNotInitialized,
      "Pipeline not initialized: {operation} called before pipeline setup",
      "Super-resolution pipeline is not ready.",
      "Initialize the pipeline before starting processing" },

    { VSRError::PipelineCancelled,
      "Pipeline cancelled by user request",
      "Super-resolution task was cancelled.",
      "Restart the task if you want to continue" },

    { VSRError::UnsupportedFormat,
      "Unsupported format: {format}",
      "The selected format is not supported.",
      "Use a supported format for output" },

    { VSRError::InvalidConfiguration,
      "Invalid configuration: {parameter}={value}",
      "Super-resolution configuration is invalid.",
      "Check your settings and try again" },

    { VSRError::Unknown,
      "Unknown error occurred",
      "An unexpected error occurred during super-resolution.",
      "Try restarting the application or contact support" }
};

static_assert(sizeof(g_errorMessages) / sizeof(VSErrorMessage) == static_cast<int32_t>(VSRError::Unknown) + 1,
              "Error message array incomplete");

} // anonymous namespace

const VSErrorMessage& VSRErrorCatalog::getErrorMessage(VSRError error) {
    int32_t index = static_cast<int32_t>(error);
    if (index < 0 || index >= static_cast<int32_t>(VSRError::Unknown) + 1) {
        index = static_cast<int32_t>(VSRError::Unknown);
    }
    return g_errorMessages[index];
}

std::string VSRErrorCatalog::toUserString(VSRError error) {
    const auto& msg = getErrorMessage(error);
    if (msg.suggestedAction.empty()) {
        return std::string(msg.userMsg);
    }
    return std::string(msg.userMsg) + " " + std::string(msg.suggestedAction);
}

std::string VSRErrorCatalog::toLogString(VSRError error, std::string_view context) {
    const auto& msg = getErrorMessage(error);
    std::string result = "VSR Error [";
    result += std::to_string(static_cast<int32_t>(error));
    result += "] ";
    result += msg.technicalMsg.data();
    if (!context.empty()) {
        result += " | Context: ";
        result += context;
    }
    return result;
}

} // namespace hlplayer
