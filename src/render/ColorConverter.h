#ifndef HLPLAYER_COLORCONVERTER_H
#define HLPLAYER_COLORCONVERTER_H

#ifndef HLPLAYER_RENDER_API
# ifdef _WIN32
#   ifdef HLPLAYER_RENDER_EXPORTS
#     define HLPLAYER_RENDER_API __declspec(dllexport)
#   else
#     define HLPLAYER_RENDER_API __declspec(dllimport)
#   endif
# else
#   define HLPLAYER_RENDER_API
# endif
#endif

#include <hlplayer/GpuFrameContract.h>
#include <array>
#include <cstdint>

namespace hlplayer {
namespace render {

struct ConversionMatrix {
    std::array<float, 3> yCoeff{};
    std::array<float, 3> cbCoeff{};
    std::array<float, 3> crCoeff{};
    std::array<float, 3> offset{};
};

class HLPLAYER_RENDER_API ColorConverter {
public:
    static ConversionMatrix getMatrix(ColorSpace space, ColorRange range);

    static void convertYUV420(
        const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
        uint32_t width, uint32_t height,
        uint32_t yStride, uint32_t uvStride,
        uint8_t* rgbOut, uint32_t rgbStride,
        ColorSpace space, ColorRange range);

    static float bt601Kr();
    static float bt601Kb();
    static float bt709Kr();
    static float bt709Kb();
    static float bt2020Kr();
    static float bt2020Kb();
};

} // namespace render
} // namespace hlplayer

#endif // HLPLAYER_COLORCONVERTER_H
