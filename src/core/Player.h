#pragma once

#ifdef _WIN32
    #ifdef HLPLAYER_CORE_EXPORTS
        #define HLPLAYER_CORE_API __declspec(dllexport)
    #else
        #define HLPLAYER_CORE_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_CORE_API
#endif

namespace hlplayer {
namespace core {

class HLPLAYER_CORE_API Player {
public:
    Player();
    ~Player();

    void initialize();
    void shutdown();
};

} // namespace core
} // namespace hlplayer
