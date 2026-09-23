#pragma once
#include <LWS/Bitmap.hpp>
#include <algorithm>
#include <cmath>
#include <vector>

namespace LWS::internal
{
    // Protocol icons are square. Fit copied premultiplied pixels into that square,
    // allowing enlargement without changing the legacy Bitmap::resize contract.
    inline std::vector<std::byte> ScaleIcon(const Bitmap& bitmap, int size)
    {
        const auto source = bitmap.GetBuffer();
        std::vector<std::byte> result(size_t(size) * size * 4, std::byte{0});
        const double scale = std::min(double(size) / source.width, double(size) / source.height);
        const int width = std::max(1, int(std::lround(source.width * scale)));
        const int height = std::max(1, int(std::lround(source.height * scale)));
        const int left = (size - width) / 2, top = (size - height) / 2;
        for (int y = 0; y < height; ++y)
        {
            const double sy = std::clamp((y + .5) / scale - .5, 0., double(source.height - 1));
            const auto y0 = uint32_t(sy), y1 = std::min(y0 + 1, source.height - 1);
            for (int x = 0; x < width; ++x)
            {
                const double sx = std::clamp((x + .5) / scale - .5, 0., double(source.width - 1));
                const auto x0 = uint32_t(sx), x1 = std::min(x0 + 1, source.width - 1);
                for (size_t channel = 0; channel < 4; ++channel)
                {
                    auto at = [&](uint32_t px, uint32_t py)
                    {
                        return double(
                            std::to_integer<uint8_t>(source.pixels[size_t(py) * source.rowPitch + px * 4 + channel]));
                    };
                    const auto value = std::lerp(std::lerp(at(x0, y0), at(x1, y0), sx - x0),
                                                 std::lerp(at(x0, y1), at(x1, y1), sx - x0), sy - y0);
                    result[(size_t(y + top) * size + x + left) * 4 + channel] = std::byte(
                        std::clamp(std::lround(value), 0L, 255L));
                }
            }
        }
        return result;
    }
}  // namespace LWS::internal
