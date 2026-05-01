#include "StreamExtractor.h"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <algorithm>
#include <sstream>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <poll.h>
    #include <signal.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <unistd.h>
#endif

namespace hlplayer {
namespace extractor {

StreamExtractor::StreamExtractor(
    const std::string& ytdlpPath,
    std::chrono::seconds timeout,
    uint32_t maxRetries)
    : ytdlpPath_(ytdlpPath)
    , timeout_(timeout)
    , maxRetries_(maxRetries) {
    spdlog::info("StreamExtractor created: path='{}' timeout={}s retries={}",
                 ytdlpPath_, timeout_.count(), maxRetries_);
}

StreamExtractor::~StreamExtractor() {
    cancel();
    if (workerThread_.joinable()) {
        workerThread_.join();
    }
    spdlog::info("StreamExtractor destroyed");
}

Result<void> StreamExtractor::resolve(
    const std::string& url,
    std::function<void(Result<StreamInfo>)> callback) {

    if (running_.load()) {
        spdlog::error("resolve called while extraction is already running");
        return Result<void>::error(PlayerError::InvalidState);
    }

    cancelled_.store(false);
    running_.store(true);

    workerThread_ = std::thread(&StreamExtractor::runExtraction, this, url, std::move(callback));

    return Result<void>::success();
}

void StreamExtractor::cancel() {
    if (!running_.load()) {
        return;
    }

    cancelled_.store(true);
    spdlog::info("StreamExtractor cancellation requested");

    std::lock_guard<std::mutex> lock(processMutex_);
#ifdef _WIN32
    if (processHandle_ != nullptr) {
        TerminateProcess(processHandle_, 1);
    }
#else
    if (childPid_ > 0) {
        kill(childPid_, SIGKILL);
    }
#endif
}

uint32_t StreamExtractor::getCapabilities() const {
    return static_cast<uint32_t>(ResolverCapability::HttpProgressive) |
           static_cast<uint32_t>(ResolverCapability::Hls) |
           static_cast<uint32_t>(ResolverCapability::Dash);
}

Result<StreamInfo> StreamExtractor::parseJsonOutput(const std::string& jsonStr) {
    if (jsonStr.empty()) {
        return Result<StreamInfo>::error(PlayerError::DecodeError);
    }

    try {
        auto json = nlohmann::json::parse(jsonStr);
        StreamInfo info;

        if (json.contains("url") && json["url"].is_string()) {
            info.url = json["url"].get<std::string>();
        }

        if (json.contains("format") && json["format"].is_string()) {
            info.format = json["format"].get<std::string>();
        } else if (json.contains("ext") && json["ext"].is_string()) {
            info.format = json["ext"].get<std::string>();
        }

        if (json.contains("width") && json["width"].is_number_integer()) {
            info.width = static_cast<uint32_t>(json["width"].get<int64_t>());
        }

        if (json.contains("height") && json["height"].is_number_integer()) {
            info.height = static_cast<uint32_t>(json["height"].get<int64_t>());
        }

        if (json.contains("tbr") && json["tbr"].is_number()) {
            double tbrKbps = json["tbr"].get<double>();
            info.bitrate = static_cast<uint64_t>(tbrKbps * 1000.0);
        } else if (json.contains("bitrate") && json["bitrate"].is_number()) {
            double bitrateKbps = json["bitrate"].get<double>();
            info.bitrate = static_cast<uint64_t>(bitrateKbps * 1000.0);
        }

        if (json.contains("drm_info") && json["drm_info"].is_string()) {
            info.drmInfo = json["drm_info"].get<std::string>();
        }

        return Result<StreamInfo>::success(std::move(info));
    } catch (const nlohmann::json::exception&) {
        spdlog::error("Failed to parse yt-dlp JSON output");
        return Result<StreamInfo>::error(PlayerError::DecodeError);
    }
}

void StreamExtractor::runExtraction(
    const std::string& url,
    std::function<void(Result<StreamInfo>)> callback) {

    for (uint32_t attempt = 0; attempt <= maxRetries_; ++attempt) {
        if (cancelled_.load()) {
            callback(Result<StreamInfo>::error(PlayerError::InvalidState));
            running_.store(false);
            return;
        }

        if (attempt > 0) {
            auto backoffSecs = std::min(1u << std::min(attempt - 1u, 4u), 30u);
            auto backoff = std::chrono::seconds(backoffSecs);
            spdlog::info("Retry attempt {}/{} in {}s",
                         attempt + 1, maxRetries_ + 1, backoffSecs);

            auto deadline = std::chrono::steady_clock::now() + backoff;
            while (std::chrono::steady_clock::now() < deadline) {
                if (cancelled_.load()) {
                    callback(Result<StreamInfo>::error(PlayerError::InvalidState));
                    running_.store(false);
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }

        spdlog::info("yt-dlp extraction attempt {}/{} for: {}",
                     attempt + 1, maxRetries_ + 1, url);

        auto result = runYtdlp(url);

        if (cancelled_.load()) {
            callback(Result<StreamInfo>::error(PlayerError::InvalidState));
            running_.store(false);
            return;
        }

        if (result.hasValue()) {
            auto parsed = parseJsonOutput(result.value());
            if (parsed.hasValue()) {
                spdlog::info("Successfully extracted stream info");
                callback(std::move(parsed));
                running_.store(false);
                return;
            }
            spdlog::warn("JSON parse failed on attempt {}/{}", attempt + 1, maxRetries_ + 1);
        } else {
            spdlog::warn("yt-dlp failed on attempt {}/{}: error={}",
                         attempt + 1, maxRetries_ + 1, static_cast<int>(result.error()));
        }
    }

    spdlog::error("All {} attempts failed", maxRetries_ + 1);
    callback(Result<StreamInfo>::error(PlayerError::NetworkError));
    running_.store(false);
}

Result<std::string> StreamExtractor::runYtdlp(const std::string& url) {
#ifdef _WIN32
    return runYtdlpWindows(url);
#else
    return runYtdlpUnix(url);
#endif
}

// ============================================================================
// Windows Implementation
// ============================================================================

#ifdef _WIN32

Result<std::string> StreamExtractor::runYtdlpWindows(const std::string& url) {
    std::string cmdLine = ytdlpPath_ + " --dump-json --no-playlist " + url;

    HANDLE hReadPipe = nullptr;
    HANDLE hWritePipe = nullptr;
    SECURITY_ATTRIBUTES sa = {};
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0)) {
        spdlog::error("CreatePipe failed: {}", GetLastError());
        return Result<std::string>::error(PlayerError::Unknown);
    }

    if (!SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        return Result<std::string>::error(PlayerError::Unknown);
    }

    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    si.hStdError = hWritePipe;
    si.hStdOutput = hWritePipe;
    si.hStdInput = INVALID_HANDLE_VALUE;
    si.dwFlags = STARTF_USESTDHANDLES;

    std::vector<char> cmdBuf(cmdLine.begin(), cmdLine.end());
    cmdBuf.push_back('\0');

    PROCESS_INFORMATION pi = {};
    if (!CreateProcessA(
            nullptr,
            cmdBuf.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi)) {
        DWORD err = GetLastError();
        CloseHandle(hReadPipe);
        CloseHandle(hWritePipe);
        spdlog::error("CreateProcessA failed: {}", err);
        return Result<std::string>::error(PlayerError::Unknown);
    }

    {
        std::lock_guard<std::mutex> lock(processMutex_);
        processHandle_ = pi.hProcess;
    }

    CloseHandle(hWritePipe);

    std::string output;
    char buffer[4096];
    auto deadline = std::chrono::steady_clock::now() + timeout_;
    bool timedOut = false;

    while (!cancelled_.load()) {
        DWORD bytesAvailable = 0;
        if (!PeekNamedPipe(hReadPipe, nullptr, 0, nullptr, &bytesAvailable, nullptr)) {
            break;
        }

        if (bytesAvailable > 0) {
            DWORD bytesRead = 0;
            if (!ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr)) {
                break;
            }
            output.append(buffer, bytesRead);
        } else {
            DWORD waitResult = WaitForSingleObject(pi.hProcess, 0);
            if (waitResult == WAIT_OBJECT_0) {
                DWORD bytesRead = 0;
                while (ReadFile(hReadPipe, buffer, sizeof(buffer) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                    output.append(buffer, bytesRead);
                }
                break;
            }

            if (std::chrono::steady_clock::now() >= deadline) {
                spdlog::warn("yt-dlp timed out after {}s", timeout_.count());
                timedOut = true;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    if (timedOut || cancelled_.load()) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 1000);
    }

    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(hReadPipe);

    {
        std::lock_guard<std::mutex> lock(processMutex_);
        processHandle_ = nullptr;
    }

    if (cancelled_.load()) {
        return Result<std::string>::error(PlayerError::InvalidState);
    }

    if (timedOut) {
        return Result<std::string>::error(PlayerError::Timeout);
    }

    if (exitCode != 0) {
        spdlog::warn("yt-dlp exited with code {}", exitCode);
        return Result<std::string>::error(PlayerError::DecodeError);
    }

    return Result<std::string>::success(std::move(output));
}

#else

// ============================================================================
// Unix Implementation
// ============================================================================

Result<std::string> StreamExtractor::runYtdlpUnix(const std::string& url) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        spdlog::error("pipe() failed");
        return Result<std::string>::error(PlayerError::Unknown);
    }

    pid_t pid = fork();
    if (pid == -1) {
        close(pipefd[0]);
        close(pipefd[1]);
        spdlog::error("fork() failed");
        return Result<std::string>::error(PlayerError::Unknown);
    }

    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);

        execlp(ytdlpPath_.c_str(), ytdlpPath_.c_str(),
               "--dump-json", "--no-playlist", url.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);

    {
        std::lock_guard<std::mutex> lock(processMutex_);
        childPid_ = pid;
    }

    std::string output;
    char buffer[4096];
    auto deadline = std::chrono::steady_clock::now() + timeout_;
    bool timedOut = false;

    while (!cancelled_.load()) {
        struct pollfd pfd = {};
        pfd.fd = pipefd[0];
        pfd.events = POLLIN;

        auto now = std::chrono::steady_clock::now();
        auto remaining = deadline - now;
        int remainingMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count());

        if (remainingMs <= 0) {
            timedOut = true;
            break;
        }

        int ret = poll(&pfd, 1, remainingMs);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            ssize_t bytesRead = read(pipefd[0], buffer, sizeof(buffer) - 1);
            if (bytesRead > 0) {
                output.append(buffer, static_cast<size_t>(bytesRead));
            } else {
                break;
            }
        } else if (ret == 0) {
            timedOut = true;
            break;
        } else {
            if (errno != EINTR) break;
        }
    }

    if (timedOut || cancelled_.load()) {
        kill(pid, SIGKILL);
    }

    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    {
        std::lock_guard<std::mutex> lock(processMutex_);
        childPid_ = -1;
    }

    if (cancelled_.load()) {
        return Result<std::string>::error(PlayerError::InvalidState);
    }

    if (timedOut) {
        spdlog::warn("yt-dlp timed out after {}s", timeout_.count());
        return Result<std::string>::error(PlayerError::Timeout);
    }

    if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        spdlog::warn("yt-dlp exited with code {}", WEXITSTATUS(status));
        return Result<std::string>::error(PlayerError::DecodeError);
    }

    if (WIFSIGNALED(status)) {
        spdlog::warn("yt-dlp killed by signal {}", WTERMSIG(status));
        return Result<std::string>::error(PlayerError::Unknown);
    }

    return Result<std::string>::success(std::move(output));
}

#endif

} // namespace extractor
} // namespace hlplayer
