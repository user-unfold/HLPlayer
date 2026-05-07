#ifndef HLPLAYER_STREAMMUXER_H
#define HLPLAYER_STREAMMUXER_H

#include <hlplayer/CameraExport.h>
#include <hlplayer/CameraTypes.h>
#include <hlplayer/Result.h>
#include <FFmpegRAII.h>
#include <string>
#include <atomic>
#include <mutex>
#include <vector>

namespace hlplayer {

using namespace hlplayer::ffmpeg;

/// Concrete streaming muxer.
/// Multiplexes encoded packets and streams to RTMP/SRT destinations.
class HLPLAYER_CAMERA_API StreamMuxer {
public:
    StreamMuxer();
    ~StreamMuxer();

    StreamMuxer(const StreamMuxer&) = delete;
    StreamMuxer& operator=(const StreamMuxer&) = delete;

    /// Open streaming destination.
    /// @param url RTMP or SRT URL.
    /// @param protocol Streaming protocol (RTMP or SRT).
    /// @return Result<void>::success() on success, or an error code on failure.
    Result<void> open(const std::string& url, StreamingProtocol protocol);

    /// Write an encoded packet to the stream.
    /// @param packet Encoded packet to write.
    /// @return Result<void>::success() on success, or an error code on failure.
    Result<void> writePacket(const EncodedPacket& packet);

    /// Close the stream and release resources.
    /// @return Result<void>::success() on success, or an error code on failure.
    Result<void> close();

    /// Abort the stream immediately.
    /// Forces any pending I/O to fail, unblocking writePacket() and close().
    /// Safe to call from any thread. Idempotent.
    void abort();

    /// Add a stream to the muxer before writing packets.
    /// Must be called after open() and before the first writePacket().
    /// @param codecId Codec ID for the stream.
    /// @param timeBase Time base for the stream.
    /// @param codecParams Optional codec parameters to copy (e.g. extradata).
    /// @return Result with the new stream index on success, or an error.
    Result<uint32_t> addStream(AVCodecID codecId, const AVRational& timeBase,
                                AVCodecParameters* codecParams = nullptr);

    /// Check if stream is open.
    /// @return true if stream is open, false otherwise.
    bool isOpen() const;

    /// Check if stream has been aborted.
    /// @return true if abort() has been called.
    bool isAborted() const;

private:
    mutable std::mutex mutex_;
    std::atomic<bool> open_{false};
    std::atomic<bool> headerWritten_{false};
    std::atomic<bool> headerFailed_{false};
    std::atomic<bool> writeFailed_{false};
    std::atomic<int> consecutiveWriteErrors_{0};
    std::atomic<bool> abortFlag_{false};
    AVFormatContextPtr formatCtx_;
    std::vector<AVStream*> streams_;
    std::string url_;
    StreamingProtocol protocol_;
};

} // namespace hlplayer

#endif // HLPLAYER_STREAMMUXER_H
