#ifndef HLPLAYER_EXTRACTOR_FACTORY_H
#define HLPLAYER_EXTRACTOR_FACTORY_H

#include <hlplayer/IStreamResolver.h>
#include <hlplayer/Demuxer.h>
#include <hlplayer/HWDecoder.h>
#include <hlplayer/IAudioDecoder.h>
#include <memory>

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

HLPLAYER_EXTRACTOR_API std::unique_ptr<IDemuxer> createFFmpegDemuxer();
HLPLAYER_EXTRACTOR_API std::unique_ptr<IHWDecoder> createFFmpegVideoDecoder();
HLPLAYER_EXTRACTOR_API std::unique_ptr<IAudioDecoder> createFFmpegAudioDecoder();

} // namespace extractor
} // namespace hlplayer

#endif // HLPLAYER_EXTRACTOR_FACTORY_H
