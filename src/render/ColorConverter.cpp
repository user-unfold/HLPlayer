#include "ColorConverter.h"
#include <algorithm>
#include <cmath>

namespace hlplayer {
namespace render {

float ColorConverter::bt601Kr() { return 0.299f; }
float ColorConverter::bt601Kb() { return 0.114f; }
float ColorConverter::bt709Kr() { return 0.2126f; }
float ColorConverter::bt709Kb() { return 0.0722f; }
float ColorConverter::bt2020Kr() { return 0.2627f; }
float ColorConverter::bt2020Kb() { return 0.0593f; }

ConversionMatrix ColorConverter::getMatrix(ColorSpace space, ColorRange range) {
    const float sy = range == ColorRange::Limited ? 255.0f / 219.0f : 1.0f;
    const float sc = range == ColorRange::Limited ? 255.0f / 224.0f : 1.0f;

    float kr = 0.0f, kb = 0.0f;
    switch (space) {
        case ColorSpace::BT601:  kr = bt601Kr();  kb = bt601Kb();  break;
        case ColorSpace::BT709:  kr = bt709Kr();  kb = bt709Kb();  break;
        case ColorSpace::BT2020: kr = bt2020Kr(); kb = bt2020Kb(); break;
        case ColorSpace::sRGB:
        default:                 kr = bt709Kr();  kb = bt709Kb();  break;
    }

    const float kg = 1.0f - kr - kb;

    ConversionMatrix m{};
    m.yCoeff = {sy, sy, sy};
    m.cbCoeff = {
        0.0f,
        -2.0f * kb * (1.0f - kb) / kg * sc,
        2.0f * (1.0f - kb) * sc
    };
    m.crCoeff = {
        2.0f * (1.0f - kr) * sc,
        -2.0f * kr * (1.0f - kr) / kg * sc,
        0.0f
    };

    if (range == ColorRange::Limited) {
        const float ySub = 16.0f * sy;
        m.offset = {
            -ySub - 128.0f * m.crCoeff[0],
            -ySub - 128.0f * m.crCoeff[1] - 128.0f * m.cbCoeff[1],
            -ySub - 128.0f * m.cbCoeff[2]
        };
    } else {
        m.offset = {
            -128.0f * m.crCoeff[0],
            -128.0f * m.crCoeff[1] - 128.0f * m.cbCoeff[1],
            -128.0f * m.cbCoeff[2]
        };
    }

    return m;
}

void ColorConverter::convertYUV420(
    const uint8_t* yPlane, const uint8_t* uPlane, const uint8_t* vPlane,
    uint32_t width, uint32_t height,
    uint32_t yStride, uint32_t uvStride,
    uint8_t* rgbOut, uint32_t rgbStride,
    ColorSpace space, ColorRange range)
{
    const auto m = getMatrix(space, range);

    for (uint32_t row = 0; row < height; ++row) {
        const uint8_t* yRow = yPlane + row * yStride;
        const uint8_t* uRow = uPlane + (row / 2) * uvStride;
        const uint8_t* vRow = vPlane + (row / 2) * uvStride;
        uint8_t* rgbRow = rgbOut + row * rgbStride;

        for (uint32_t col = 0; col < width; ++col) {
            float y  = static_cast<float>(yRow[col]);
            float cb = static_cast<float>(uRow[col / 2]);
            float cr = static_cast<float>(vRow[col / 2]);

            float r = m.yCoeff[0] * y + m.cbCoeff[0] * cb + m.crCoeff[0] * cr + m.offset[0];
            float g = m.yCoeff[1] * y + m.cbCoeff[1] * cb + m.crCoeff[1] * cr + m.offset[1];
            float b = m.yCoeff[2] * y + m.cbCoeff[2] * cb + m.crCoeff[2] * cr + m.offset[2];

            auto toByte = [](float v) -> uint8_t {
                return static_cast<uint8_t>(std::clamp(v, 0.0f, 255.0f));
            };

            rgbRow[col * 3 + 0] = toByte(r);
            rgbRow[col * 3 + 1] = toByte(g);
            rgbRow[col * 3 + 2] = toByte(b);
        }
    }
}

} // namespace render
} // namespace hlplayer
