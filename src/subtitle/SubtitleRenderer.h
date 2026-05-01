#ifndef HLPLAYER_SUBTITLE_RENDERER_H
#define HLPLAYER_SUBTITLE_RENDERER_H

#include <atomic>
#include <cstdint>
#include <string>

#ifdef _WIN32
    #ifdef HLPLAYER_SUBTITLE_EXPORTS
        #define HLPLAYER_SUBTITLE_API __declspec(dllexport)
    #else
        #define HLPLAYER_SUBTITLE_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_SUBTITLE_API
#endif

namespace hlplayer {
namespace subtitle {

enum class SubtitleSource : int {
    None = 0,
    External,
    Embedded
};

/// Manages subtitle state and generates FFmpeg subtitles filter descriptions.
/// Phase 1: external .srt/.ass via FFmpeg subtitles filter; embedded tracks stored for Phase 2.
class HLPLAYER_SUBTITLE_API SubtitleRenderer {
public:
    SubtitleRenderer();
    ~SubtitleRenderer();

    SubtitleRenderer(const SubtitleRenderer&) = delete;
    SubtitleRenderer& operator=(const SubtitleRenderer&) = delete;

    bool loadFile(const std::string& path);
    void loadFromStream(int streamIndex, int codecId);

    /// Returns FFmpeg filter string (e.g. "subtitles='path.srt'") or "" if none/off.
    std::string getFilterDescription() const;

    bool hasSubtitles() const;
    void setVisibility(bool visible);
    bool isVisible() const;
    bool toggleVisibility();

    /// Scans video's directory for video_name.{srt,ass,ssa}. Returns path or "".
    std::string autoDiscover(const std::string& videoPath) const;

    const std::string& subtitlePath() const;
    SubtitleSource source() const;
    void reset();

private:
    bool validateFile(const std::string& path) const;
    static std::string escapeFilterPath(const std::string& path);

    std::string subtitlePath_;
    std::atomic<SubtitleSource> source_{SubtitleSource::None};
    std::atomic<bool> visible_{true};
    int embeddedStreamIndex_ = -1;
    int embeddedCodecId_ = 0;
};

} // namespace subtitle
} // namespace hlplayer

#endif // HLPLAYER_SUBTITLE_RENDERER_H
