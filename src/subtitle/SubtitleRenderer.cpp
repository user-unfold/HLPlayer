#include "SubtitleRenderer.h"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <filesystem>
#include <fstream>

namespace hlplayer {
namespace subtitle {

namespace fs = std::filesystem;

SubtitleRenderer::SubtitleRenderer() {
    spdlog::info("SubtitleRenderer created");
}

SubtitleRenderer::~SubtitleRenderer() {
    spdlog::info("SubtitleRenderer destroyed");
}

bool SubtitleRenderer::loadFile(const std::string& path) {
    if (!validateFile(path)) {
        spdlog::warn("SubtitleRenderer: failed to load subtitle file '{}'", path);
        return false;
    }

    subtitlePath_ = path;
    source_.store(SubtitleSource::External);
    visible_.store(true);
    spdlog::info("SubtitleRenderer: loaded external subtitle '{}'", path);
    return true;
}

void SubtitleRenderer::loadFromStream(int streamIndex, int codecId) {
    embeddedStreamIndex_ = streamIndex;
    embeddedCodecId_ = codecId;
    source_.store(SubtitleSource::Embedded);
    visible_.store(true);
    spdlog::info("SubtitleRenderer: registered embedded subtitle stream index={} codecId={}",
                 streamIndex, codecId);
}

std::string SubtitleRenderer::getFilterDescription() const {
    if (!visible_.load()) {
        return {};
    }

    auto src = source_.load();
    if (src == SubtitleSource::External && !subtitlePath_.empty()) {
        return "subtitles=" + escapeFilterPath(subtitlePath_);
    }

    if (src == SubtitleSource::Embedded) {
        spdlog::warn("SubtitleRenderer: embedded subtitle filter not yet implemented (Phase 2)");
    }

    return {};
}

bool SubtitleRenderer::hasSubtitles() const {
    return source_.load() != SubtitleSource::None;
}

void SubtitleRenderer::setVisibility(bool visible) {
    bool prev = visible_.exchange(visible);
    if (prev != visible) {
        spdlog::info("SubtitleRenderer: visibility changed to {}", visible ? "on" : "off");
    }
}

bool SubtitleRenderer::isVisible() const {
    return visible_.load();
}

bool SubtitleRenderer::toggleVisibility() {
    bool newVal = !visible_.exchange(!visible_.load());
    visible_.store(newVal);
    spdlog::info("SubtitleRenderer: toggled visibility to {}", newVal ? "on" : "off");
    return newVal;
}

std::string SubtitleRenderer::autoDiscover(const std::string& videoPath) const {
    fs::path videoFile(videoPath);
    if (videoFile.extension().empty()) {
        return {};
    }

    fs::path dir = videoFile.parent_path();
    std::string stem = videoFile.stem().string();

    static const char* extensions[] = {".srt", ".ass", ".ssa"};

    for (const char* ext : extensions) {
        fs::path candidate = dir / (stem + ext);
        std::error_code ec;
        if (fs::exists(candidate, ec) && fs::file_size(candidate, ec) > 0) {
            spdlog::info("SubtitleRenderer: auto-discovered subtitle '{}'", candidate.string());
            return candidate.string();
        }
    }

    return {};
}

const std::string& SubtitleRenderer::subtitlePath() const {
    return subtitlePath_;
}

SubtitleSource SubtitleRenderer::source() const {
    return source_.load();
}

void SubtitleRenderer::reset() {
    subtitlePath_.clear();
    source_.store(SubtitleSource::None);
    visible_.store(true);
    embeddedStreamIndex_ = -1;
    embeddedCodecId_ = 0;
    spdlog::info("SubtitleRenderer: reset");
}

bool SubtitleRenderer::validateFile(const std::string& path) const {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        spdlog::warn("SubtitleRenderer: file does not exist: '{}'", path);
        return false;
    }

    auto size = fs::file_size(path, ec);
    if (size == 0) {
        spdlog::warn("SubtitleRenderer: file is empty: '{}'", path);
        return false;
    }

    if (size > 100 * 1024 * 1024) {
        spdlog::warn("SubtitleRenderer: file too large ({} bytes), skipping: '{}'", size, path);
        return false;
    }

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
        spdlog::warn("SubtitleRenderer: cannot open file: '{}'", path);
        return false;
    }

    char firstBytes[16] = {};
    ifs.read(firstBytes, sizeof(firstBytes));
    auto bytesRead = ifs.gcount();

    if (bytesRead == 0) {
        spdlog::warn("SubtitleRenderer: could not read file header: '{}'", path);
        return false;
    }

    std::string header(firstBytes, static_cast<size_t>(bytesRead));

    if (header.find("[Script Info]") != std::string::npos) {
        return true;
    }

    if (header.find("1\r\n") != std::string::npos ||
        header.find("1\n") != std::string::npos ||
        header.find("00:") != std::string::npos ||
        header.find("WEBVTT") != std::string::npos) {
        return true;
    }

    spdlog::warn("SubtitleRenderer: file '{}' does not appear to be a valid subtitle file", path);
    return false;
}

std::string SubtitleRenderer::escapeFilterPath(const std::string& path) {
    std::string escaped = "'";
    for (char c : path) {
        if (c == '\'') {
            escaped += "'\\''";
        } else if (c == '\\') {
            escaped += "/";  // FFmpeg filter uses / for path separators
        } else if (c == ':') {
            escaped += "\\:";
        } else {
            escaped += c;
        }
    }
    escaped += "'";
    return escaped;
}

} // namespace subtitle
} // namespace hlplayer
