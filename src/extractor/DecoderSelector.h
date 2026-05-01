#ifndef HLPLAYER_DECODERSELECTOR_H
#define HLPLAYER_DECODERSELECTOR_H

#include <hlplayer/HWDecoder.h>
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

class EventBus;

class HLPLAYER_EXTRACTOR_API DecoderSelector {
public:
    explicit DecoderSelector(EventBus* eventBus = nullptr);
    ~DecoderSelector();

    DecoderSelector(const DecoderSelector&) = delete;
    DecoderSelector& operator=(const DecoderSelector&) = delete;

    std::unique_ptr<IHWDecoder> selectDecoder(Codec codec, const DecoderConfig& config);

private:
    EventBus* eventBus_;
};

} // namespace hlplayer

#endif // HLPLAYER_DECODERSELECTOR_H
