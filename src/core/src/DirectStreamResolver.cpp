#include <hlplayer/DirectStreamResolver.h>
#include <hlplayer/logger.h>

namespace hlplayer {

Result<void> DirectStreamResolver::resolve(
    const std::string& url,
    std::function<void(Result<StreamInfo>)> callback)
{
    if (url.empty()) {
        if (callback) {
            callback(Result<StreamInfo>::error(PlayerError::InvalidURL));
        }
        return Result<void>::error(PlayerError::InvalidURL);
    }

    LOG_INFO("DirectStreamResolver: passthrough for \"{}\"", url);

    StreamInfo info;
    info.url = url;
    info.format = "auto";

    if (callback) {
        callback(Result<StreamInfo>::success(std::move(info)));
    }

    return Result<void>::success();
}

void DirectStreamResolver::cancel() {}

uint32_t DirectStreamResolver::getCapabilities() const {
    return static_cast<uint32_t>(ResolverCapability::HttpProgressive);
}

} // namespace hlplayer
