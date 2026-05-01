#include <hlplayer/DefaultStreamResolver.h>
#include <hlplayer/logger.h>
#include <algorithm>
#include <regex>
#include <sstream>

namespace hlplayer {

// ============================================================================
// MockURLSnifferPlugin Implementation
// ============================================================================

MockURLSnifferPlugin::MockURLSnifferPlugin(
    const std::string& domainPattern,
    const std::string& resolvedUrlTemplate,
    const std::string& format)
    : domainPattern_(domainPattern)
    , resolvedUrlTemplate_(resolvedUrlTemplate)
    , format_(format)
{
}

Result<void> MockURLSnifferPlugin::resolve(
    const std::string& webpageUrl,
    StreamInfo& streamInfo)
{
    LOG_INFO("MockURLSnifferPlugin: Resolving URL: {}", webpageUrl);

    std::string resolvedUrl = resolvedUrlTemplate_;
    size_t pos = resolvedUrl.find("{url}");
    if (pos != std::string::npos) {
        resolvedUrl.replace(pos, 5, webpageUrl);
    }

    streamInfo.url = resolvedUrl;
    streamInfo.format = format_;
    streamInfo.width = 1920;
    streamInfo.height = 1080;
    streamInfo.bitrate = 5000000;

    LOG_INFO("MockURLSnifferPlugin: Resolved to: {}", resolvedUrl);
    return Result<void>::success();
}

bool MockURLSnifferPlugin::canHandle(const std::string& url) const {
    return url.find(domainPattern_) != std::string::npos;
}

// ============================================================================
// DefaultStreamResolver Implementation
// ============================================================================

DefaultStreamResolver::DefaultStreamResolver(bool enablePlugins)
    : enablePlugins_(enablePlugins)
    , cancelled_(false)
    , capabilities_(
        static_cast<uint32_t>(ResolverCapability::HttpProgressive) |
        static_cast<uint32_t>(ResolverCapability::Hls) |
        static_cast<uint32_t>(ResolverCapability::Dash) |
        static_cast<uint32_t>(ResolverCapability::Rtsp))
{
    LOG_INFO("DefaultStreamResolver created (plugins: {})", enablePlugins);
}

Result<void> DefaultStreamResolver::resolve(
    const std::string& url,
    std::function<void(Result<StreamInfo>)> callback)
{
    LOG_INFO("DefaultStreamResolver::resolve called with URL: {}", url);

    if (url.empty()) {
        LOG_ERROR("URL is empty");
        if (callback) {
            callback(Result<StreamInfo>::error(PlayerError::InvalidURL));
        }
        return Result<void>::error(PlayerError::InvalidURL);
    }

    // Check if operation is cancelled
    if (cancelled_) {
        LOG_WARN("Resolve operation cancelled before starting");
        if (callback) {
            callback(Result<StreamInfo>::error(PlayerError::Unknown));
        }
        return Result<void>::error(PlayerError::Unknown);
    }

    StreamInfo info;
    Result<void> resolveResult = Result<void>::success();

    if (isDirectStreamURL(url)) {
        resolveResult = resolveDirect(url, info);
    } else if (enablePlugins_) {
        resolveResult = resolveWithPlugins(url, info);
    } else {
        LOG_WARN("Plugins disabled, cannot resolve URL: {}", url);
        resolveResult = Result<void>::error(PlayerError::UnsupportedFormat);
    }

    if (resolveResult.hasValue()) {
        LOG_INFO("Resolve succeeded: {} -> {}", url, info.url);
        if (callback) {
            callback(Result<StreamInfo>::success(info));
        }
    } else {
        LOG_ERROR("Resolve failed for URL: {}", url);
        if (callback) {
            callback(Result<StreamInfo>::error(resolveResult.error()));
        }
    }

    return Result<void>::success();
}

void DefaultStreamResolver::cancel() {
    LOG_INFO("DefaultStreamResolver::cancel called");
    cancelled_ = true;
}

uint32_t DefaultStreamResolver::getCapabilities() const {
    return capabilities_;
}

void DefaultStreamResolver::registerPlugin(std::unique_ptr<IURLSnifferPlugin> plugin) {
    if (!plugin) {
        LOG_WARN("Attempted to register null plugin");
        return;
    }

    std::lock_guard<std::mutex> lock(pluginsMutex_);
    plugins_.push_back(std::move(plugin));
    LOG_INFO("Registered plugin: {}, total plugins: {}",
             plugins_.back()->getName(), plugins_.size());
}

bool DefaultStreamResolver::unregisterPlugin(const std::string& pluginName) {
    std::lock_guard<std::mutex> lock(pluginsMutex_);

    auto it = std::remove_if(plugins_.begin(), plugins_.end(),
        [&pluginName](const std::unique_ptr<IURLSnifferPlugin>& plugin) {
            return std::string(plugin->getName()) == pluginName;
        });

    if (it != plugins_.end()) {
        size_t count = std::distance(it, plugins_.end());
        plugins_.erase(it, plugins_.end());
        LOG_INFO("Unregistered {} plugin(s) with name: {}", count, pluginName);
        return true;
    }

    LOG_WARN("No plugin found with name: {}", pluginName);
    return false;
}

size_t DefaultStreamResolver::getPluginCount() const {
    std::lock_guard<std::mutex> lock(pluginsMutex_);
    return plugins_.size();
}

bool DefaultStreamResolver::isDirectStreamURL(const std::string& url) const {
    if (url.find("rtmp://") == 0) {
        return true;
    }
    if (url.find("rtsp://") == 0) {
        return true;
    }

    std::vector<std::string> extensions = {".m3u8", ".mp4", ".flv", ".ts", ".mpd", ".mkv"};
    for (const auto& ext : extensions) {
        if (url.find(ext) != std::string::npos) {
            size_t pos = url.find(ext);
            size_t nextChar = pos + ext.length();
            if (nextChar == url.length() || url[nextChar] == '?' || url[nextChar] == '#') {
                return true;
            }
        }
    }

    return false;
}

Result<void> DefaultStreamResolver::resolveDirect(
    const std::string& url,
    StreamInfo& info) const
{
    LOG_INFO("Direct passthrough for URL: {}", url);

    info.url = url;

    if (url.find(".m3u8") != std::string::npos) {
        info.format = "hls";
    } else if (url.find(".mpd") != std::string::npos) {
        info.format = "dash";
    } else if (url.find(".mp4") != std::string::npos) {
        info.format = "mp4";
    } else if (url.find(".flv") != std::string::npos) {
        info.format = "flv";
    } else if (url.find("rtmp://") == 0) {
        info.format = "rtmp";
    } else if (url.find("rtsp://") == 0) {
        info.format = "rtsp";
    } else {
        info.format = "unknown";
    }

    info.width = 0;
    info.height = 0;
    info.bitrate = 0;

    return Result<void>::success();
}

Result<void> DefaultStreamResolver::resolveWithPlugins(
    const std::string& url,
    StreamInfo& info) const
{
    LOG_INFO("Attempting to resolve URL with plugins: {}", url);

    std::lock_guard<std::mutex> lock(pluginsMutex_);

    for (auto it = plugins_.rbegin(); it != plugins_.rend(); ++it) {
        auto& plugin = *it;
        if (plugin->canHandle(url)) {
            LOG_INFO("Plugin {} can handle URL: {}", plugin->getName(), url);
            return plugin->resolve(url, info);
        }
    }

    LOG_WARN("No plugin found that can handle URL: {}", url);
    return Result<void>::error(PlayerError::UnsupportedFormat);
}

void DefaultStreamResolver::executeResolve(
    const std::string& url,
    std::function<void(Result<StreamInfo>)> callback)
{
    (void)url;
    (void)callback;
}

std::unique_ptr<IURLSnifferPlugin>
URLSnifferPluginFactory::createMockPlugin(
    const std::string& domainPattern,
    const std::string& resolvedUrlTemplate,
    const std::string& format)
{
    LOG_INFO("Creating mock plugin for domain: {}, format: {}",
             domainPattern, format);
    return std::make_unique<MockURLSnifferPlugin>(
        domainPattern, resolvedUrlTemplate, format);
}

std::unique_ptr<IURLSnifferPlugin>
URLSnifferPluginFactory::loadFromFile(const std::string& pluginPath)
{
    LOG_WARN("loadFromFile not implemented for v1, path: {}", pluginPath);
    (void)pluginPath;
    return nullptr;
}

} // namespace hlplayer
