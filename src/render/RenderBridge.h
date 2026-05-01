#pragma once

#ifdef _WIN32
    #ifdef HLPLAYER_RENDER_EXPORTS
        #define HLPLAYER_RENDER_API __declspec(dllexport)
    #else
        #define HLPLAYER_RENDER_API __declspec(dllimport)
    #endif
#else
    #define HLPLAYER_RENDER_API
#endif

namespace hlplayer {
namespace render {

class HLPLAYER_RENDER_API RenderBridge {
public:
    RenderBridge();
    ~RenderBridge();

    void initialize();
    void render();
};

} // namespace render
} // namespace hlplayer
