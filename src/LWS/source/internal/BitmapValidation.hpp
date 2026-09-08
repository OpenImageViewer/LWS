#pragma once

#include <LWS/Bitmap.hpp>

#include <limits>
#include <optional>

namespace LWS::internal
{
    struct BitmapLayout
    {
        size_t bytesPerPixel;
        size_t rowPitch;
    };

    [[nodiscard]] inline std::optional<BitmapLayout> validateBitmapBuffer(const BitmapBuffer& bitmap)
    {
        size_t bytesPerPixel = 0;
        switch (bitmap.format)
        {
            case BitmapPixelFormat::Bgr8:
                bytesPerPixel = 3;
                break;
            case BitmapPixelFormat::Bgra8:
            case BitmapPixelFormat::Bgra8Premultiplied:
                bytesPerPixel = 4;
                break;
            default:
                return std::nullopt;
        }

        if ((bitmap.rowOrder != BitmapRowOrder::TopDown && bitmap.rowOrder != BitmapRowOrder::BottomUp) ||
            bitmap.width == 0 || bitmap.height == 0 ||
            static_cast<size_t>(bitmap.width) > std::numeric_limits<size_t>::max() / bytesPerPixel)
        {
            return std::nullopt;
        }

        const size_t minimumRowPitch = static_cast<size_t>(bitmap.width) * bytesPerPixel;
        const size_t rowPitch = bitmap.rowPitch == 0 ? minimumRowPitch : bitmap.rowPitch;
        if (rowPitch < minimumRowPitch ||
            static_cast<size_t>(bitmap.height) > std::numeric_limits<size_t>::max() / rowPitch)
            return std::nullopt;

        if (bitmap.pixels.size() < rowPitch * bitmap.height)
            return std::nullopt;

        return BitmapLayout{.bytesPerPixel = bytesPerPixel, .rowPitch = rowPitch};
    }
}  // namespace LWS::internal
