#include "DecoderSelector.h"
#include <hlplayer/CPUFallbackDecoder.h>
#include <hlplayer/EventBus.h>
#include <hlplayer/logger.h>

#include "FFmpegVideoDecoder.h"

#include <vector>

namespace hlplayer {

DecoderSelector::DecoderSelector(EventBus* eventBus)
    : eventBus_(eventBus) {}

DecoderSelector::~DecoderSelector() = default;

namespace {

struct BackendCandidate {
    DecodeBackend backend;
    const char* name;
};

std::vector<BackendCandidate> platformCandidates() {
#if defined(_WIN32)
    return {
        {DecodeBackend::D3D11, "D3D11VA"},
        {DecodeBackend::D3D11, "DXVA2"},
    };
#elif defined(__APPLE__)
    return {
        {DecodeBackend::Auto, "VideoToolbox"},
    };
#elif defined(__linux__)
    return {
        {DecodeBackend::Auto, "VAAPI"},
    };
#else
    return {};
#endif
}

} // namespace

std::unique_ptr<IHWDecoder> DecoderSelector::selectDecoder(Codec codec, const DecoderConfig& config) {
    auto candidates = platformCandidates();
    DecoderConfig hwConfig = config;
    hwConfig.codec = codec;

    for (const auto& candidate : candidates) {
        hwConfig.backend = candidate.backend;

        LOG_INFO("DecoderSelector: trying {} for codec {}", candidate.name, static_cast<int>(codec));

        auto decoder = std::make_unique<extractor::FFmpegVideoDecoder>(eventBus_);
        auto result = decoder->open(hwConfig);

        if (result.hasValue()) {
            LOG_INFO("DecoderSelector: {} opened successfully", candidate.name);
            return decoder;
        }

        LOG_WARN("DecoderSelector: {} failed (error {}), trying next backend",
                 candidate.name, static_cast<int>(result.error()));
    }

    LOG_WARN("DecoderSelector: all hardware backends failed, falling back to CPU");
    auto cpuDecoder = std::make_unique<CPUFallbackDecoder>();
    hwConfig.backend = DecodeBackend::CPU;

    auto cpuResult = cpuDecoder->open(hwConfig);
    if (cpuResult.hasValue()) {
        return cpuDecoder;
    }

    LOG_ERROR("DecoderSelector: CPU fallback also failed (error {})", static_cast<int>(cpuResult.error()));
    return nullptr;
}

} // namespace hlplayer
