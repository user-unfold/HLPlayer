#ifndef HLPLAYER_SUBTITLE_MANAGER_H
#define HLPLAYER_SUBTITLE_MANAGER_H

#include <hlplayer/ASRExport.h>
#include <hlplayer/ASRTypes.h>

#include <mutex>
#include <string>
#include <vector>

namespace hlplayer {
namespace asr {

/// Manages subtitle history and provides SRT export functionality.
class HLPLAYER_ASR_API SubtitleManager {
public:
    SubtitleManager();
    ~SubtitleManager();

    /// Add a new subtitle segment to the history.
    void addSegment(const SubtitleSegment& segment);

    /// Add multiple subtitle segments.
    void addSegments(const std::vector<SubtitleSegment>& segments);

    /// Get the most recent N segments (for display).
    std::vector<SubtitleSegment> getRecent(size_t count = 2) const;

    /// Get all recorded segments (for export).
    std::vector<SubtitleSegment> getAll() const;

    /// Get total number of recorded segments.
    size_t count() const;

    /// Clear all recorded segments.
    void clear();

    /// Export all segments to SRT format string.
    std::string exportSRT(bool includeTranslation = false) const;

    /// Export all segments to an SRT file.
    /// @return true on success
    bool exportSRTFile(const std::string& filePath, bool includeTranslation = false) const;

private:
    /// Format a timestamp as SRT time code (HH:MM:SS,mmm).
    static std::string formatSRTTime(double seconds);

    std::vector<SubtitleSegment> segments_;
    mutable std::mutex mutex_;
};

} // namespace asr
} // namespace hlplayer

#endif // HLPLAYER_SUBTITLE_MANAGER_H
