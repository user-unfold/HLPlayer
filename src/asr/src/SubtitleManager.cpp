#include <hlplayer/SubtitleManager.h>

#include <spdlog/spdlog.h>

#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace hlplayer {
namespace asr {

SubtitleManager::SubtitleManager() = default;
SubtitleManager::~SubtitleManager() = default;

void SubtitleManager::addSegment(const SubtitleSegment& segment) {
    std::lock_guard<std::mutex> lock(mutex_);
    segments_.push_back(segment);
}

void SubtitleManager::addSegments(const std::vector<SubtitleSegment>& segments) {
    std::lock_guard<std::mutex> lock(mutex_);
    segments_.insert(segments_.end(), segments.begin(), segments.end());
}

std::vector<SubtitleSegment> SubtitleManager::getRecent(size_t count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (segments_.empty()) {
        return {};
    }
    size_t start = segments_.size() > count ? segments_.size() - count : 0;
    return {segments_.begin() + start, segments_.end()};
}

std::vector<SubtitleSegment> SubtitleManager::getAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return segments_;
}

size_t SubtitleManager::count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return segments_.size();
}

void SubtitleManager::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    segments_.clear();
}

std::string SubtitleManager::formatSRTTime(double seconds) {
    if (seconds < 0.0) seconds = 0.0;

    int totalMs = static_cast<int>(std::round(seconds * 1000.0));
    int hours = totalMs / 3600000;
    totalMs %= 3600000;
    int minutes = totalMs / 60000;
    totalMs %= 60000;
    int secs = totalMs / 1000;
    int ms = totalMs % 1000;

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << hours << ":"
        << std::setw(2) << minutes << ":"
        << std::setw(2) << secs << ","
        << std::setw(3) << ms;
    return oss.str();
}

std::string SubtitleManager::exportSRT(bool includeTranslation) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ostringstream oss;
    int index = 1;

    for (const auto& seg : segments_) {
        oss << index++ << "\n";
        oss << formatSRTTime(seg.startTime) << " --> " << formatSRTTime(seg.endTime) << "\n";
        oss << seg.text << "\n";
        if (includeTranslation && !seg.translation.empty()) {
            oss << seg.translation << "\n";
        }
        oss << "\n";
    }

    return oss.str();
}

bool SubtitleManager::exportSRTFile(const std::string& filePath, bool includeTranslation) const {
    std::string content = exportSRT(includeTranslation);

    std::ofstream file(filePath, std::ios::out | std::ios::trunc);
    if (!file.is_open()) {
        spdlog::error("SubtitleManager: failed to open file for writing: {}", filePath);
        return false;
    }

    // Write UTF-8 BOM for compatibility with some subtitle editors
    file << "\xEF\xBB\xBF";
    file << content;
    file.close();

    spdlog::info("SubtitleManager: exported {} segments to {}", count(), filePath);
    return true;
}

} // namespace asr
} // namespace hlplayer
