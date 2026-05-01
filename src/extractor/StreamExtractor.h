#pragma once

#include <hlplayer/IStreamResolver.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#ifdef _WIN32
    #ifdef HLPLAYER_EXTRACTOR_EXPORTS
        #define HLPLAYER_EXTRACTOR_API __declspec(dllexport)
    #else
        #define HLPLAYER_EXTRACTOR_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_EXTRACTOR_API
#endif

namespace hlplayer {
namespace extractor {

class HLPLAYER_EXTRACTOR_API StreamExtractor : public hlplayer::IStreamResolver {
public:
    explicit StreamExtractor(
        const std::string& ytdlpPath = "yt-dlp",
        std::chrono::seconds timeout = std::chrono::seconds(10),
        uint32_t maxRetries = 3);

    ~StreamExtractor() override;

    StreamExtractor(const StreamExtractor&) = delete;
    StreamExtractor& operator=(const StreamExtractor&) = delete;

    Result<void> resolve(
        const std::string& url,
        std::function<void(Result<StreamInfo>)> callback) override;

    void cancel() override;

    uint32_t getCapabilities() const override;

    static Result<StreamInfo> parseJsonOutput(const std::string& jsonStr);

private:
    void runExtraction(
        const std::string& url,
        std::function<void(Result<StreamInfo>)> callback);

    Result<std::string> runYtdlp(const std::string& url);

#ifdef _WIN32
    Result<std::string> runYtdlpWindows(const std::string& url);
#else
    Result<std::string> runYtdlpUnix(const std::string& url);
#endif

    std::string ytdlpPath_;
    std::chrono::seconds timeout_;
    uint32_t maxRetries_;

    std::atomic<bool> cancelled_{false};
    std::atomic<bool> running_{false};
    std::thread workerThread_;
    std::mutex processMutex_;

#ifdef _WIN32
    void* processHandle_ = nullptr;
#else
    pid_t childPid_ = -1;
#endif
};

} // namespace extractor
} // namespace hlplayer
