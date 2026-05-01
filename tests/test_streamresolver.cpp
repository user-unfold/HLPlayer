#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <hlplayer/DefaultStreamResolver.h>
#include <hlplayer/IStreamResolver.h>
#include <hlplayer/Result.h>
#include <chrono>
#include <thread>
#include <memory>

using namespace hlplayer;

class TestURLSnifferPlugin : public hlplayer::IURLSnifferPlugin {
public:
    TestURLSnifferPlugin(
        const std::string& domainPattern,
        const std::string& resolvedUrl,
        const std::string& format)
        : domainPattern_(domainPattern)
        , resolvedUrl_(resolvedUrl)
        , format_(format)
    {
    }

    Result<void> resolve(const std::string& webpageUrl, StreamInfo& streamInfo) override {
        streamInfo.url = resolvedUrl_;
        streamInfo.format = format_;
        streamInfo.width = 1920;
        streamInfo.height = 1080;
        streamInfo.bitrate = 5000000;
        return Result<void>::success();
    }

    bool canHandle(const std::string& url) const override {
        return url.find(domainPattern_) != std::string::npos;
    }

    const char* getName() const override {
        return "TestURLSnifferPlugin";
    }

private:
    std::string domainPattern_;
    std::string resolvedUrl_;
    std::string format_;
};

TEST_CASE("StreamInfo default construction", "[streamresolver][basics]") {
    StreamInfo info;
    REQUIRE(info.url.empty());
    REQUIRE(info.format.empty());
    REQUIRE(info.width == 0);
    REQUIRE(info.height == 0);
    REQUIRE(info.bitrate == 0);
    REQUIRE(info.drmInfo.empty());
}

TEST_CASE("StreamInfo field assignment", "[streamresolver][basics]") {
    StreamInfo info;
    info.url = "http://example.com/video.mp4";
    info.format = "mp4";
    info.width = 1920;
    info.height = 1080;
    info.bitrate = 5000000;
    info.drmInfo = "widevine";

    REQUIRE(info.url == "http://example.com/video.mp4");
    REQUIRE(info.format == "mp4");
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.bitrate == 5000000);
    REQUIRE(info.drmInfo == "widevine");
}

TEST_CASE("TestURLSnifferPlugin resolve", "[streamresolver][plugin]") {
    TestURLSnifferPlugin plugin(
        "example.com",
        "https://cdn.example.com/stream.m3u8",
        "m3u8");

    StreamInfo info;
    auto result = plugin.resolve("https://example.com/video", info);

    REQUIRE(result.hasValue());
    REQUIRE(info.url == "https://cdn.example.com/stream.m3u8");
    REQUIRE(info.format == "m3u8");
    REQUIRE(info.width == 1920);
    REQUIRE(info.height == 1080);
    REQUIRE(info.bitrate == 5000000);
}

TEST_CASE("TestURLSnifferPlugin canHandle", "[streamresolver][plugin]") {
    TestURLSnifferPlugin plugin("example.com", "https://cdn.example.com/stream.m3u8", "m3u8");

    REQUIRE(plugin.canHandle("https://example.com/video"));
    REQUIRE(plugin.canHandle("http://example.com/stream"));
    REQUIRE_FALSE(plugin.canHandle("https://other-site.com/video"));
    REQUIRE(plugin.getName() == std::string("TestURLSnifferPlugin"));
}

TEST_CASE("DefaultStreamResolver construction", "[streamresolver][basics]") {
    DefaultStreamResolver resolver;
    REQUIRE(resolver.getCapabilities() != 0);
    REQUIRE(resolver.getPluginCount() == 0);

    uint32_t caps = resolver.getCapabilities();
    REQUIRE((caps & static_cast<uint32_t>(ResolverCapability::HttpProgressive)) != 0);
    REQUIRE((caps & static_cast<uint32_t>(ResolverCapability::Hls)) != 0);
    REQUIRE((caps & static_cast<uint32_t>(ResolverCapability::Dash)) != 0);
    REQUIRE((caps & static_cast<uint32_t>(ResolverCapability::Rtsp)) != 0);
}

TEST_CASE("DefaultStreamResolver direct passthrough for m3u8", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/stream.m3u8",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://example.com/stream.m3u8");
    REQUIRE(resolvedInfo.format == "hls");
}

TEST_CASE("DefaultStreamResolver direct passthrough for mp4", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/video.mp4",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://example.com/video.mp4");
    REQUIRE(resolvedInfo.format == "mp4");
}

TEST_CASE("DefaultStreamResolver direct passthrough for rtmp", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "rtmp://example.com/live/stream",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "rtmp://example.com/live/stream");
    REQUIRE(resolvedInfo.format == "rtmp");
}

TEST_CASE("DefaultStreamResolver direct passthrough for rtsp", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "rtsp://example.com/stream",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "rtsp://example.com/stream");
    REQUIRE(resolvedInfo.format == "rtsp");
}

TEST_CASE("DefaultStreamResolver with plugin: fake-site.com", "[streamresolver][plugin]") {
    DefaultStreamResolver resolver(true);

    auto plugin = std::make_unique<TestURLSnifferPlugin>(
        "fake-site.com",
        "https://cdn.fake-site.com/stream.m3u8",
        "m3u8");
    resolver.registerPlugin(std::move(plugin));

    REQUIRE(resolver.getPluginCount() == 1);

    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://fake-site.com/video",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://cdn.fake-site.com/stream.m3u8");
    REQUIRE(resolvedInfo.format == "m3u8");
    REQUIRE(resolvedInfo.width == 1920);
    REQUIRE(resolvedInfo.height == 1080);
    REQUIRE(resolvedInfo.bitrate == 5000000);
}

TEST_CASE("DefaultStreamResolver plugin with query parameters", "[streamresolver][plugin]") {
    DefaultStreamResolver resolver(true);

    auto plugin = std::make_unique<TestURLSnifferPlugin>(
        "example.com",
        "https://cdn.example.com/stream.m3u8?token=xyz",
        "m3u8");
    resolver.registerPlugin(std::move(plugin));

    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/video?id=123",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://cdn.example.com/stream.m3u8?token=xyz");
}

TEST_CASE("DefaultStreamResolver plugin unregister", "[streamresolver][plugin]") {
    DefaultStreamResolver resolver(true);

    auto plugin = std::make_unique<TestURLSnifferPlugin>(
        "fake-site.com",
        "https://cdn.fake-site.com/stream.m3u8",
        "m3u8");
    resolver.registerPlugin(std::move(plugin));

    REQUIRE(resolver.getPluginCount() == 1);

    bool removed = resolver.unregisterPlugin("TestURLSnifferPlugin");
    REQUIRE(removed);
    REQUIRE(resolver.getPluginCount() == 0);

    bool callbackCalled = false;
    auto result = resolver.resolve(
        "https://fake-site.com/video",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE_FALSE(r.hasValue());
            REQUIRE(r.error() == PlayerError::UnsupportedFormat);
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
}

TEST_CASE("DefaultStreamResolver multiple plugins priority", "[streamresolver][plugin]") {
    DefaultStreamResolver resolver(true);

    auto plugin1 = std::make_unique<TestURLSnifferPlugin>(
        "site.com",
        "https://cdn1.site.com/stream.m3u8",
        "m3u8");
    resolver.registerPlugin(std::move(plugin1));

    auto plugin2 = std::make_unique<TestURLSnifferPlugin>(
        "site.com",
        "https://cdn2.site.com/stream.m3u8",
        "m3u8");
    resolver.registerPlugin(std::move(plugin2));

    REQUIRE(resolver.getPluginCount() == 2);

    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://site.com/video",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://cdn2.site.com/stream.m3u8");
}

TEST_CASE("DefaultStreamResolver empty URL returns error", "[streamresolver][error]") {
    DefaultStreamResolver resolver;
    bool callbackCalled = false;

    auto result = resolver.resolve(
        "",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE_FALSE(r.hasValue());
            REQUIRE(r.error() == PlayerError::InvalidURL);
        });

    REQUIRE_FALSE(result.hasValue());
    REQUIRE(result.error() == PlayerError::InvalidURL);
}

TEST_CASE("DefaultStreamResolver unsupported URL without plugins", "[streamresolver][error]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;

    auto result = resolver.resolve(
        "https://unsupported-site.com/video",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE_FALSE(r.hasValue());
            REQUIRE(r.error() == PlayerError::UnsupportedFormat);
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
}

TEST_CASE("DefaultStreamResolver cancel", "[streamresolver][cancel]") {
    DefaultStreamResolver resolver;
    resolver.cancel();
}

TEST_CASE("DefaultStreamResolver no plugin for URL", "[streamresolver][error]") {
    DefaultStreamResolver resolver(true);

    bool callbackCalled = false;
    auto result = resolver.resolve(
        "https://no-matching-plugin.com/video",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE_FALSE(r.hasValue());
            REQUIRE(r.error() == PlayerError::UnsupportedFormat);
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
}

TEST_CASE("URLSnifferPluginFactory createMockPlugin", "[streamresolver][factory]") {
    auto plugin = std::make_unique<TestURLSnifferPlugin>(
        "example.com",
        "https://cdn.example.com/stream.m3u8",
        "m3u8");

    REQUIRE(plugin != nullptr);
    REQUIRE(plugin->getName() == std::string("TestURLSnifferPlugin"));
    REQUIRE(plugin->canHandle("https://example.com/video"));

    StreamInfo info;
    auto result = plugin->resolve("https://example.com/video", info);
    REQUIRE(result.hasValue());
    REQUIRE(info.url == "https://cdn.example.com/stream.m3u8");
}

TEST_CASE("DefaultStreamResolver direct passthrough dash", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/stream.mpd",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://example.com/stream.mpd");
    REQUIRE(resolvedInfo.format == "dash");
}

TEST_CASE("DefaultStreamResolver direct passthrough flv", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/stream.flv",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://example.com/stream.flv");
    REQUIRE(resolvedInfo.format == "flv");
}

TEST_CASE("DefaultStreamResolver direct passthrough with query params", "[streamresolver][passthrough]") {
    DefaultStreamResolver resolver(false);
    bool callbackCalled = false;
    StreamInfo resolvedInfo;

    auto result = resolver.resolve(
        "https://example.com/stream.m3u8?token=abc&quality=1080p",
        [&](Result<StreamInfo> r) {
            callbackCalled = true;
            REQUIRE(r.hasValue());
            resolvedInfo = r.value();
        });

    REQUIRE(result.hasValue());

    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    REQUIRE(callbackCalled);
    REQUIRE(resolvedInfo.url == "https://example.com/stream.m3u8?token=abc&quality=1080p");
    REQUIRE(resolvedInfo.format == "hls");
}

TEST_CASE("DefaultStreamResolver plugin register null", "[streamresolver][plugin]") {
    DefaultStreamResolver resolver;
    size_t countBefore = resolver.getPluginCount();

    resolver.registerPlugin(nullptr);

    REQUIRE(resolver.getPluginCount() == countBefore);
}
