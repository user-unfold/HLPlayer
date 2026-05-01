#ifndef HLPLAYER_DIRECTSTREAMRESOLVER_H
#define HLPLAYER_DIRECTSTREAMRESOLVER_H

#include <hlplayer/IStreamResolver.h>

namespace hlplayer {

/// Simple synchronous stream resolver for local files and direct media URLs.
/// Immediately invokes the callback with the URL as-is — no network probing,
/// no yt-dlp, no plugin system. Suitable for file:// paths, bare file paths,
/// and direct HTTP/HTTPS media URLs.
class HLPLAYER_CORE_API DirectStreamResolver final : public IStreamResolver {
public:
    DirectStreamResolver() = default;
    ~DirectStreamResolver() override = default;

    Result<void> resolve(const std::string& url,
                         std::function<void(Result<StreamInfo>)> callback) override;
    void cancel() override;
    uint32_t getCapabilities() const override;
};

} // namespace hlplayer

#endif // HLPLAYER_DIRECTSTREAMRESOLVER_H
