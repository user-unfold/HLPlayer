#ifndef HLPLAYER_VSRERRORCATALOG_H
#define HLPLAYER_VSRERRORCATALOG_H

#include <string>
#include <string_view>

#include <hlplayer/Export.h>

namespace hlplayer {

enum class VSRError {
    None = 0,
    ModelNotFound,
    ModelLoadFailed,
    ModelCorrupted,
    InferenceFailed,
    InferenceTimeout,
    VRAMExceeded,
    VRAMAllocationFailed,
    EncoderNotFound,
    EncoderInitFailed,
    EncodingFailed,
    MuxerOpenFailed,
    MuxerWriteFailed,
    MuxerFinalizeFailed,
    CheckpointSaveFailed,
    CheckpointRestoreFailed,
    CheckpointCorrupted,
    PipelineAlreadyRunning,
    PipelineNotInitialized,
    PipelineCancelled,
    UnsupportedFormat,
    InvalidConfiguration,
    Unknown
};

struct VSErrorMessage {
    VSRError code;
    std::string_view technicalMsg;
    std::string_view userMsg;
    std::string_view suggestedAction;
};

class HLPLAYER_CORE_API VSRErrorCatalog {
public:
    static const VSErrorMessage& getErrorMessage(VSRError error);

    static std::string toUserString(VSRError error);

    static std::string toLogString(VSRError error, std::string_view context = "");

private:
    VSRErrorCatalog() = default;
};

} // namespace hlplayer

#endif // HLPLAYER_VSRERRORCATALOG_H
