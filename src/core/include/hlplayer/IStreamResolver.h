#ifndef HLPLAYER_ISTREAMRESOLVER_H
#define HLPLAYER_ISTREAMRESOLVER_H

#include <hlplayer/Result.h>
#include <cstdint>
#include <functional>
#include <string>

namespace hlplayer {

enum class ResolverCapability : uint32_t {
    None = 0,
    HttpProgressive = 1u << 0,
    Hls = 1u << 1,
    Dash = 1u << 2,
    Rtsp = 1u << 3
};

struct StreamInfo {
    std::string url;
    std::string format;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t bitrate = 0;
    std::string drmInfo;
};

class HLPLAYER_CORE_API IStreamResolver {
public:
    virtual ~IStreamResolver() = default;

    virtual Result<void> resolve(
        const std::string& url,
        std::function<void(Result<StreamInfo>)> callback) = 0;

    virtual void cancel() = 0;

    virtual uint32_t getCapabilities() const = 0;
};

} // namespace hlplayer

#endif // HLPLAYER_ISTREAMRESOLVER_H
