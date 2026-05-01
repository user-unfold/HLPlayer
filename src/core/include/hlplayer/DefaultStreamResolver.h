#ifndef HLPLAYER_DEFAULTSTREAMRESOLVER_H
#define HLPLAYER_DEFAULTSTREAMRESOLVER_H

#include <hlplayer/IStreamResolver.h>
#include <hlplayer/Result.h>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace hlplayer {

// ============================================================================
// Plugin Interface for URL Sniffing/Extraction
// ============================================================================

/**
 * @brief Abstract plugin interface for resolving webpage URLs to stream URLs
 *
 * This interface isolates web scraping logic from the core SDK to mitigate
 * DMCA compliance risks. Plugins can be loaded dynamically (.dll/.so) or
 * mocked for testing purposes.
 *
 * Example use cases:
 * - Extracting m3u8 URL from a video webpage
 * - Resolving CDN URLs from a video sharing platform
 * - Bypassing URL shorteners or redirects
 */
class IURLSnifferPlugin {
public:
    virtual ~IURLSnifferPlugin() = default;

    /**
     * @brief Resolve a webpage URL to actual stream URL(s)
     *
     * @param webpageUrl The user-provided URL (e.g., https://site.com/video)
     * @param[out] streamInfo Output stream information including resolved URL
     * @return Result<void> Success if resolved, error if failed
     */
    virtual Result<void> resolve(
        const std::string& webpageUrl,
        StreamInfo& streamInfo) = 0;

    /**
     * @brief Check if this plugin can handle the given URL
     *
     * @param url URL to check
     * @return true if this plugin can handle the URL
     */
    virtual bool canHandle(const std::string& url) const = 0;

    /**
     * @brief Get the plugin name for debugging/logging
     */
    virtual const char* getName() const = 0;
};

// ============================================================================
// Mock Plugin for Testing/Demo Purposes
// ============================================================================

/**
 * @brief Mock plugin that simulates URL extraction
 *
 * For v1, this is a dummy implementation that demonstrates the plugin
 * architecture without implementing actual web scraping.
 */
class MockURLSnifferPlugin : public IURLSnifferPlugin {
public:
    explicit MockURLSnifferPlugin(
        const std::string& domainPattern,
        const std::string& resolvedUrlTemplate,
        const std::string& format = "m3u8");

    Result<void> resolve(
        const std::string& webpageUrl,
        StreamInfo& streamInfo) override;

    bool canHandle(const std::string& url) const override;

    const char* getName() const override { return "MockURLSnifferPlugin"; }

private:
    std::string domainPattern_;
    std::string resolvedUrlTemplate_;
    std::string format_;
};

// ============================================================================
// Default Stream Resolver Implementation
// ============================================================================

/**
 * @brief Default implementation of IStreamResolver with plugin support
 *
 * This resolver supports two strategies:
 * 1. Direct Passthrough: Returns the URL as-is for known stream formats
 *    (.m3u8, .mp4, rtmp://, etc.)
 * 2. Plugin-based: Loads plugins to extract stream URLs from webpages
 *
 * The resolver is designed to be thread-safe and supports async operations
 * through callbacks.
 */
class HLPLAYER_CORE_API DefaultStreamResolver : public IStreamResolver {
public:
    /**
     * @brief Construct a DefaultStreamResolver
     *
     * @param enablePlugins Enable plugin-based URL extraction
     */
    explicit DefaultStreamResolver(bool enablePlugins = true);

    ~DefaultStreamResolver() override = default;

    // =========================================================================
    // IStreamResolver Implementation
    // =========================================================================

    /**
     * @brief Resolve a URL to stream information
     *
     * For direct URLs (.m3u8, .mp4, rtmp://), the URL is passed through
     * with minimal validation. For other URLs, registered plugins are tried
     * in order until one can handle the URL.
     *
     * @param url URL to resolve
     * @param callback Async callback with result
     * @return Result<void> Success if resolve initiated, error if invalid URL
     */
    Result<void> resolve(
        const std::string& url,
        std::function<void(Result<StreamInfo>)> callback) override;

    /**
     * @brief Cancel any pending resolve operations
     */
    void cancel() override;

    /**
     * @brief Get resolver capabilities
     *
     * @return Bitmask of ResolverCapability flags
     */
    uint32_t getCapabilities() const override;

    // =========================================================================
    // Plugin Management
    // =========================================================================

    /**
     * @brief Register a plugin for URL extraction
     *
     * Plugins are checked in registration order (last registered has priority)
     *
     * @param plugin Plugin to register (ownership transferred)
     */
    void registerPlugin(std::unique_ptr<IURLSnifferPlugin> plugin);

    /**
     * @brief Unregister a plugin by name
     *
     * @param pluginName Name of plugin to remove
     * @return true if plugin was found and removed
     */
    bool unregisterPlugin(const std::string& pluginName);

    /**
     * @brief Get number of registered plugins
     */
    size_t getPluginCount() const;

private:
    // =========================================================================
    // Internal Helper Methods
    // =========================================================================

    /**
     * @brief Check if URL is a direct stream URL (passthrough)
     *
     * @param url URL to check
     * @return true if URL should be passed through directly
     */
    bool isDirectStreamURL(const std::string& url) const;

    /**
     * @brief Resolve URL using direct passthrough
     *
     * @param url URL to resolve
     * @param[out] info Stream info to populate
     * @return Result<void> Always success for direct URLs
     */
    Result<void> resolveDirect(const std::string& url, StreamInfo& info) const;

    /**
     * @brief Resolve URL using plugins
     *
     * @param url URL to resolve
     * @param[out] info Stream info to populate
     * @return Result<void> Success if a plugin resolved the URL
     */
    Result<void> resolveWithPlugins(const std::string& url, StreamInfo& info) const;

    /**
     * @brief Execute resolve operation on background thread
     *
     * @param url URL to resolve
     * @param callback Completion callback
     */
    void executeResolve(
        const std::string& url,
        std::function<void(Result<StreamInfo>)> callback);

    // =========================================================================
    // Member Variables
    // =========================================================================

    mutable std::mutex pluginsMutex_;
    std::vector<std::unique_ptr<IURLSnifferPlugin>> plugins_;

    bool enablePlugins_;
    bool cancelled_;

    // Capability bitmask
    uint32_t capabilities_;
};

// ============================================================================
// Plugin Factory (for future dynamic loading)
// ============================================================================

/**
 * @brief Factory for creating plugin instances
 *
 * In future versions, this can load plugins from .dll/.so files.
 * For v1, it creates mock plugins for demonstration.
 */
class URLSnifferPluginFactory {
public:
    /**
     * @brief Create a mock plugin for testing
     *
     * @param domainPattern Domain pattern to match (e.g., "fake-site.com")
     * @param resolvedUrlTemplate Template for resolved URL with {url} placeholder
     * @param format Stream format (e.g., "m3u8")
     * @return Unique pointer to the plugin
     */
    static std::unique_ptr<IURLSnifferPlugin> createMockPlugin(
        const std::string& domainPattern,
        const std::string& resolvedUrlTemplate,
        const std::string& format = "m3u8");

    /**
     * @brief Load plugin from file path (future feature)
     *
     * @param pluginPath Path to .dll/.so file
     * @return Unique pointer to loaded plugin, or nullptr on failure
     */
    static std::unique_ptr<IURLSnifferPlugin> loadFromFile(const std::string& pluginPath);
};

} // namespace hlplayer

#endif // HLPLAYER_DEFAULTSTREAMRESOLVER_H
