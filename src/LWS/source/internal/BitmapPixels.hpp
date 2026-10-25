#pragma once
#include "BitmapValidation.hpp"
#include <cstring>
namespace LWS::internal
{
    inline uint8_t premultiply(uint8_t channel, uint8_t alpha)
    {
        return static_cast<uint8_t>((static_cast<uint16_t>(channel) * alpha + 127U) / 255U);
    }
    // The caller validates layout and owns a tightly packed BGRA8 destination. This routine neither allocates
    // nor validates again, preserving the row-copy fast path for already premultiplied pixels.
    inline void copyBitmapPixels(const BitmapBuffer& source, BitmapLayout layout, std::byte* destination)
    {
        const size_t rowSize = static_cast<size_t>(source.width) * 4U;
        if (source.format == BitmapPixelFormat::Bgra8Premultiplied)
        {
            for (uint32_t y = 0; y < source.height; ++y)
            {
                const uint32_t sourceY = source.rowOrder == BitmapRowOrder::TopDown ? y : source.height - y - 1;
                std::memcpy(destination + y * rowSize, source.pixels.data() + sourceY * layout.rowPitch, rowSize);
            }
            return;
        }
        for (uint32_t y = 0; y < source.height; ++y)
        {
            const uint32_t sourceY = source.rowOrder == BitmapRowOrder::TopDown ? y : source.height - y - 1;
            const auto* sourceRow = reinterpret_cast<const uint8_t*>(source.pixels.data()) + sourceY * layout.rowPitch;
            auto* targetRow = reinterpret_cast<uint8_t*>(destination) + y * rowSize;
            for (uint32_t x = 0; x < source.width; ++x)
            {
                const auto* pixel = sourceRow + static_cast<size_t>(x) * layout.bytesPerPixel;
                auto* target = targetRow + static_cast<size_t>(x) * 4U;
                const uint8_t alpha = layout.bytesPerPixel == 4 ? pixel[3] : 255U;
                for (size_t channel = 0; channel < 3; ++channel)
                    target[channel] = source.format == BitmapPixelFormat::Bgra8 ? premultiply(pixel[channel], alpha)
                                                                                : pixel[channel];
                target[3] = alpha;
            }
        }
    }
}  // namespace LWS::internal
