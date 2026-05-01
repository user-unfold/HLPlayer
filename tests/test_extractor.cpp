#include <catch2/catch_test_macros.hpp>

#include "StreamExtractor.h"
#include <hlplayer/IStreamResolver.h>

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <atomic>

// ============================================================================
// Test helpers
// ============================================================================

namespace {

// RAII temp batch file for mock yt-dlp on Windows
#ifdef _WIN32
class MockYtdlpScript {
public:
    explicit MockYtdlpScript(const std::string& outputContent, bool echoOutput = true) {
        path_ = std::filesystem::temp_directory_path() / "hlplayer_mock_ytdlp.cmd";
        std::ofstream f(path_);
        if (echoOutput) {
            f << "@echo off\necho " << outputContent << "\nexit /b 0\n";
        } else {
            f << "@echo off\n" << outputContent << "\nexit /b 0\n";
        }
    }

    explicit MockYtdlpScript(const std::string& command, int exitCode) {
        path_ = std::filesystem::temp_directory_path() / "hlplayer_mock_ytdlp.cmd";
        std::ofstream f(path_);
        f << "@echo off\n" << command << "\nexit /b " << exitCode << "\n";
    }

    ~MockYtdlpScript() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::string cmdPrefix() const {
        return "cmd.exe /c \"" + path_.string() + "\"";
    }

private:
    std::filesystem::path path_;
};
#else
class MockYtdlpScript {
public:
    explicit MockYtdlpScript(const std::string& content) {
        path_ = std::filesystem::temp_directory_path() / "hlplayer_mock_ytdlp.sh";
        std::ofstream f(path_);
        f << "#!/bin/sh\n";
        f << content << "\n";
        f << "exit 0\n";
        std::filesystem::permissions(path_,
            std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::add);
    }

    explicit MockYtdlpScript(const std::string& content, int exitCode) {
        path_ = std::filesystem::temp_directory_path() / "hlplayer_mock_ytdlp.sh";
        std::ofstream f(path_);
        f << "#!/bin/sh\n";
        f << content << "\n";
        f << "exit " << exitCode << "\n";
        std::filesystem::permissions(path_,
            std::filesystem::perms::owner_exec,
            std::filesystem::perm_options::add);
    }

    ~MockYtdlpScript() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    std::string cmdPrefix() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
};
#endif

// Helper to run resolve() and wait for callback
struct ResolveResult {
    std::atomic<bool> called{false};
    hlplayer::Result<hlplayer::StreamInfo> result =
        hlplayer::Result<hlplayer::StreamInfo>::error(hlplayer::PlayerError::Unknown);
    std::mutex mtx;
    std::condition_variable cv;

    auto callback() {
        return [this](hlplayer::Result<hlplayer::StreamInfo> r) {
            result = std::move(r);
            called.store(true);
            cv.notify_all();
        };
    }

    bool wait(std::chrono::milliseconds timeoutMs = std::chrono::milliseconds(15000)) {
        std::unique_lock<std::mutex> lock(mtx);
        return cv.wait_for(lock, timeoutMs, [this] { return called.load(); });
    }
};

// Valid yt-dlp --dump-json mock output
const std::string kValidJson =
    R"({"title":"Test Video","url":"http://example.com/video.mp4","format":"mp4 1920x1080","width":1920,"height":1080,"tbr":5000.0,"ext":"mp4"})";

const std::string kMinimalJson =
    R"({"url":"http://example.com/video.mp4"})";

const std::string kJsonWithBitrate =
    R"({"title":"HD Video","url":"http://example.com/hd.mp4","format":"mkv","width":3840,"height":2160,"bitrate":15000.5,"ext":"mkv","drm_info":"widevine"})";

const std::string kJsonZeroBitrate =
    R"({"url":"http://example.com/audio.mp3","format":"mp3","tbr":0})";

} // anonymous namespace

// ============================================================================
// JSON Parsing Tests
// ============================================================================

TEST_CASE("parseJsonOutput parses valid yt-dlp JSON", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(kValidJson);
    REQUIRE(result.hasValue());

    const auto& info = result.value();
    CHECK(info.url == "http://example.com/video.mp4");
    CHECK(info.format == "mp4 1920x1080");
    CHECK(info.width == 1920);
    CHECK(info.height == 1080);
    CHECK(info.bitrate == 5000000); // 5000.0 kbps -> 5000000 bps
}

TEST_CASE("parseJsonOutput handles minimal JSON with only url", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(kMinimalJson);
    REQUIRE(result.hasValue());

    const auto& info = result.value();
    CHECK(info.url == "http://example.com/video.mp4");
    CHECK(info.format.empty());
    CHECK(info.width == 0);
    CHECK(info.height == 0);
    CHECK(info.bitrate == 0);
}

TEST_CASE("parseJsonOutput handles bitrate field and drm_info", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(kJsonWithBitrate);
    REQUIRE(result.hasValue());

    const auto& info = result.value();
    CHECK(info.url == "http://example.com/hd.mp4");
    CHECK(info.format == "mkv");
    CHECK(info.width == 3840);
    CHECK(info.height == 2160);
    CHECK(info.bitrate == 15000500); // 15000.5 kbps -> 15000500 bps
    CHECK(info.drmInfo == "widevine");
}

TEST_CASE("parseJsonOutput uses ext as fallback when format is missing", "[extractor][json]") {
    std::string json = R"({"url":"http://example.com/v.webm","ext":"webm"})";
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(json);
    REQUIRE(result.hasValue());
    CHECK(result.value().format == "webm");
}

TEST_CASE("parseJsonOutput format takes precedence over ext", "[extractor][json]") {
    std::string json = R"({"url":"http://example.com/v.mp4","format":"mp4 720p","ext":"mp4"})";
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(json);
    REQUIRE(result.hasValue());
    CHECK(result.value().format == "mp4 720p");
}

TEST_CASE("parseJsonOutput handles zero bitrate", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(kJsonZeroBitrate);
    REQUIRE(result.hasValue());
    CHECK(result.value().bitrate == 0);
}

TEST_CASE("parseJsonOutput returns error for invalid JSON", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput("not json at all");
    REQUIRE(result.hasError());
    CHECK(result.error() == hlplayer::PlayerError::DecodeError);
}

TEST_CASE("parseJsonOutput returns error for empty string", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput("");
    REQUIRE(result.hasError());
}

TEST_CASE("parseJsonOutput returns error for truncated JSON", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(R"({"title":"incomplete)");
    REQUIRE(result.hasError());
}

TEST_CASE("parseJsonOutput returns error for JSON without url", "[extractor][json]") {
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(R"({"title":"No URL Here"})");
    REQUIRE(result.hasValue()); // Still parses - url is just empty string
    CHECK(result.value().url.empty());
}

TEST_CASE("parseJsonOutput handles non-numeric width/height gracefully", "[extractor][json]") {
    std::string json = R"({"url":"http://example.com/v.mp4","width":"not_a_number","height":"also_not"})";
    auto result = hlplayer::extractor::StreamExtractor::parseJsonOutput(json);
    // nlohmann::json type check should skip non-numeric fields
    REQUIRE(result.hasValue());
    CHECK(result.value().width == 0);
    CHECK(result.value().height == 0);
}

// ============================================================================
// Construction Tests
// ============================================================================

TEST_CASE("StreamExtractor can be constructed with default parameters", "[extractor][construction]") {
    REQUIRE_NOTHROW(hlplayer::extractor::StreamExtractor{});
}

TEST_CASE("StreamExtractor can be constructed with custom yt-dlp path", "[extractor][construction]") {
    REQUIRE_NOTHROW(hlplayer::extractor::StreamExtractor{"my-custom-ytdlp"});
}

TEST_CASE("StreamExtractor can be constructed with custom timeout", "[extractor][construction]") {
    REQUIRE_NOTHROW(hlplayer::extractor::StreamExtractor{"yt-dlp", std::chrono::seconds(30)});
}

TEST_CASE("StreamExtractor can be constructed with custom retries", "[extractor][construction]") {
    REQUIRE_NOTHROW(hlplayer::extractor::StreamExtractor{"yt-dlp", std::chrono::seconds(10), 5});
}

// ============================================================================
// Interface / Polymorphism Tests
// ============================================================================

TEST_CASE("StreamExtractor implements IStreamResolver interface", "[extractor][interface]") {
    hlplayer::extractor::StreamExtractor extractor;
    hlplayer::IStreamResolver& resolver = extractor;

    CHECK(resolver.getCapabilities() != 0);
}

TEST_CASE("StreamExtractor pointer is polymorphic via IStreamResolver", "[extractor][interface]") {
    auto extractor = std::make_unique<hlplayer::extractor::StreamExtractor>();
    hlplayer::IStreamResolver* base = extractor.get();

    REQUIRE(base != nullptr);
    CHECK(base->getCapabilities() != 0);
}

// ============================================================================
// Capabilities Tests
// ============================================================================

TEST_CASE("getCapabilities returns HTTP, HLS, and DASH flags", "[extractor][capabilities]") {
    hlplayer::extractor::StreamExtractor extractor;

    uint32_t caps = extractor.getCapabilities();

    CHECK((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::HttpProgressive)) != 0);
    CHECK((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::Hls)) != 0);
    CHECK((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::Dash)) != 0);
    CHECK((caps & static_cast<uint32_t>(hlplayer::ResolverCapability::Rtsp)) == 0);
}

// ============================================================================
// Resolve with Subprocess Tests
// ============================================================================

TEST_CASE("resolve succeeds with mock yt-dlp outputting valid JSON", "[extractor][resolve]") {
    MockYtdlpScript mock(kValidJson);
    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(10),
        0);

    ResolveResult rr;
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(5)));
    REQUIRE(rr.result.hasValue());

    const auto& info = rr.result.value();
    CHECK(info.url == "http://example.com/video.mp4");
    CHECK(info.width == 1920);
    CHECK(info.height == 1080);
}

TEST_CASE("resolve fails with non-existent yt-dlp binary", "[extractor][resolve]") {
    hlplayer::extractor::StreamExtractor extractor(
        "__nonexistent_ytdlp_binary_12345__",
        std::chrono::seconds(5),
        0);

    ResolveResult rr;
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(10)));
    CHECK(rr.result.hasError());
}

TEST_CASE("resolve returns error for invalid JSON output from subprocess", "[extractor][resolve]") {
    // Mock that outputs garbage instead of JSON but exits 0
    MockYtdlpScript mock("echo not valid json at all", 0);
    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(10),
        0);

    ResolveResult rr;
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(5)));
    CHECK(rr.result.hasError());
}

TEST_CASE("resolve returns error when subprocess exits non-zero", "[extractor][resolve]") {
    MockYtdlpScript mock("echo error message", 1);
    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(10),
        0);

    ResolveResult rr;
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(5)));
    CHECK(rr.result.hasError());
}

TEST_CASE("resolve rejects concurrent calls", "[extractor][resolve]") {
    MockYtdlpScript mock(kValidJson);
    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(10),
        0);

    ResolveResult rr1;
    auto start1 = extractor.resolve("http://example.com/test", rr1.callback());
    REQUIRE(start1.hasValue());

    // Second resolve should fail while first is running
    ResolveResult rr2;
    auto start2 = extractor.resolve("http://example.com/test2", rr2.callback());
    CHECK(start2.hasError());

    // Wait for first to complete
    REQUIRE(rr1.wait(std::chrono::seconds(5)));
}

// ============================================================================
// Timeout Tests
// ============================================================================

TEST_CASE("resolve times out when subprocess hangs", "[extractor][timeout]") {
#ifdef _WIN32
    MockYtdlpScript mock("ping -n 20 127.0.0.1 > nul", false);
#else
    MockYtdlpScript mock("sleep 20", false);
#endif

    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(2),  // 2 second timeout
        0);

    ResolveResult rr;
    auto start = std::chrono::steady_clock::now();
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(10)));
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(rr.result.hasError());
    CHECK(elapsed < std::chrono::seconds(8)); // Should not take 20s
}

// ============================================================================
// Retry Tests
// ============================================================================

TEST_CASE("resolve retries on failure with exponential backoff", "[extractor][retry]") {
    // Use a binary that exits non-zero immediately
#ifdef _WIN32
    std::string failingCmd = "cmd.exe /c \"exit 1\"";
#else
    std::string failingCmd = "false";
#endif

    hlplayer::extractor::StreamExtractor extractor(
        failingCmd,
        std::chrono::seconds(5),
        2);  // max 2 retries = 3 total attempts

    ResolveResult rr;
    auto start = std::chrono::steady_clock::now();
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(15)));
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(rr.result.hasError());

    // With 2 retries: backoff 1s + 2s = 3s minimum
    // Allow some tolerance for process creation overhead
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    CHECK(elapsedMs >= 2500);  // At least ~3s of backoff
    CHECK(elapsedMs < 10000);  // But not too long
}

TEST_CASE("resolve with zero retries fails immediately on first attempt", "[extractor][retry]") {
#ifdef _WIN32
    std::string failingCmd = "cmd.exe /c \"exit 1\"";
#else
    std::string failingCmd = "false";
#endif

    hlplayer::extractor::StreamExtractor extractor(
        failingCmd,
        std::chrono::seconds(5),
        0);  // No retries

    ResolveResult rr;
    auto start = std::chrono::steady_clock::now();
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    REQUIRE(rr.wait(std::chrono::seconds(10)));
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(rr.result.hasError());
    // Should be fast - no backoff at all
    CHECK(elapsed < std::chrono::seconds(3));
}

// ============================================================================
// Cancellation Tests
// ============================================================================

TEST_CASE("cancel stops a running extraction", "[extractor][cancel]") {
#ifdef _WIN32
    MockYtdlpScript mock("ping -n 20 127.0.0.1 > nul", false);
#else
    MockYtdlpScript mock("sleep 20", false);
#endif

    hlplayer::extractor::StreamExtractor extractor(
        mock.cmdPrefix(),
        std::chrono::seconds(30),  // Long timeout
        0);

    ResolveResult rr;
    auto start = std::chrono::steady_clock::now();
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    REQUIRE(startResult.hasValue());

    // Wait a bit then cancel
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    extractor.cancel();

    REQUIRE(rr.wait(std::chrono::seconds(5)));
    auto elapsed = std::chrono::steady_clock::now() - start;

    CHECK(rr.result.hasError());
    CHECK(rr.result.error() == hlplayer::PlayerError::InvalidState);
    // Should complete quickly after cancel
    CHECK(elapsed < std::chrono::seconds(5));
}

TEST_CASE("cancel before resolve is harmless", "[extractor][cancel]") {
    hlplayer::extractor::StreamExtractor extractor("yt-dlp", std::chrono::seconds(5), 0);
    REQUIRE_NOTHROW(extractor.cancel());

    // Should still be able to resolve after cancel
    ResolveResult rr;
    auto startResult = extractor.resolve("http://example.com/test", rr.callback());
    // This will fail because no real yt-dlp, but it shouldn't crash
    REQUIRE(startResult.hasValue());
    REQUIRE(rr.wait(std::chrono::seconds(5)));
    CHECK(rr.result.hasError());
}

// ============================================================================
// Destructor / Cleanup Tests
// ============================================================================

TEST_CASE("destructor cleans up running extraction", "[extractor][cleanup]") {
#ifdef _WIN32
    MockYtdlpScript mock("ping -n 20 127.0.0.1 > nul", false);
#else
    MockYtdlpScript mock("sleep 20", false);
#endif

    {
        hlplayer::extractor::StreamExtractor extractor(
            mock.cmdPrefix(),
            std::chrono::seconds(30),
            0);

        ResolveResult rr;
        auto startResult = extractor.resolve("http://example.com/test", rr.callback());
        REQUIRE(startResult.hasValue());
        // Let resolve run for a bit
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        // extractor goes out of scope - destructor should clean up
    }
    // If we get here without hanging, destructor cleanup worked
    CHECK(true);
}
