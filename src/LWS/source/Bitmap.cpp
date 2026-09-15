#include <LWS/Bitmap.hpp>
#include <LWS/source/internal/BitmapValidation.hpp>

#include <LLUtils/Exception.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>

namespace
{
    uint8_t Premultiply(uint8_t channel, uint8_t alpha)
    {
        return static_cast<uint8_t>((static_cast<uint16_t>(channel) * alpha + 127U) / 255U);
    }
}  // namespace

namespace LWS
{
    class Bitmap::Impl
    {
      public:

        explicit Impl(const BitmapBuffer& source)
        {
            const auto sourceLayout = internal::validateBitmapBuffer(source);
            if (!sourceLayout.has_value() || source.width > std::numeric_limits<uint32_t>::max() / 4U ||
                source.height > std::numeric_limits<size_t>::max() / 4U / source.width)
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Invalid bitmap layout");

            fWidth = source.width;
            fHeight = source.height;
            fPixels = std::make_unique_for_overwrite<std::byte[]>(static_cast<size_t>(fWidth) * fHeight * 4U);
            if (source.format == BitmapPixelFormat::Bgra8Premultiplied)
            {
                const size_t rowSize = static_cast<size_t>(fWidth) * 4U;
                for (uint32_t y = 0; y < fHeight; ++y)
                {
                    const uint32_t sourceY = source.rowOrder == BitmapRowOrder::TopDown ? y : fHeight - y - 1;
                    std::memcpy(fPixels.get() + static_cast<size_t>(y) * rowSize,
                                source.pixels.data() + static_cast<size_t>(sourceY) * sourceLayout->rowPitch, rowSize);
                }
                return;
            }

            for (uint32_t y = 0; y < fHeight; ++y)
            {
                const uint32_t sourceY = source.rowOrder == BitmapRowOrder::TopDown ? y : fHeight - y - 1;
                const auto* sourceRow = reinterpret_cast<const uint8_t*>(source.pixels.data()) +
                                        static_cast<size_t>(sourceY) * sourceLayout->rowPitch;
                auto* targetRow = reinterpret_cast<uint8_t*>(fPixels.get()) + static_cast<size_t>(y) * fWidth * 4U;
                for (uint32_t x = 0; x < fWidth; ++x)
                {
                    const auto* sourcePixel = sourceRow + static_cast<size_t>(x) * sourceLayout->bytesPerPixel;
                    auto* targetPixel = targetRow + static_cast<size_t>(x) * 4U;
                    const uint8_t alpha = sourceLayout->bytesPerPixel == 4 ? sourcePixel[3] : 255U;
                    targetPixel[0] = source.format == BitmapPixelFormat::Bgra8 ? Premultiply(sourcePixel[0], alpha)
                                                                               : sourcePixel[0];
                    targetPixel[1] = source.format == BitmapPixelFormat::Bgra8 ? Premultiply(sourcePixel[1], alpha)
                                                                               : sourcePixel[1];
                    targetPixel[2] = source.format == BitmapPixelFormat::Bgra8 ? Premultiply(sourcePixel[2], alpha)
                                                                               : sourcePixel[2];
                    targetPixel[3] = alpha;
                }
            }
        }

        Impl(uint32_t width, uint32_t height, std::unique_ptr<std::byte[]> pixels)
            : fWidth(width), fHeight(height), fPixels(std::move(pixels))
        {
        }

        std::unique_ptr<Impl> Resize(int width, int height, LLUtils::Color background) const
        {
            if (width <= 0 || height <= 0 || static_cast<uint32_t>(width) > std::numeric_limits<uint32_t>::max() / 4U)
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Invalid bitmap size");
            if (static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / 4U / static_cast<size_t>(height))
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Bitmap is too large");
            }

            auto pixels = std::make_unique_for_overwrite<std::byte[]>(static_cast<size_t>(width) * height * 4U);
            const uint8_t alpha = background.A();
            const std::array backgroundPixel{Premultiply(background.B(), alpha), Premultiply(background.G(), alpha),
                                             Premultiply(background.R(), alpha), alpha};

            const double scale = std::min(
                {1.0, static_cast<double>(width) / fWidth, static_cast<double>(height) / fHeight});
            const int scaledWidth = std::max(1, static_cast<int>(std::lround(fWidth * scale)));
            const int scaledHeight = std::max(1, static_cast<int>(std::lround(fHeight * scale)));
            const int offsetX = (width - scaledWidth) / 2;
            const int offsetY = (height - scaledHeight) / 2;
            // Initialize only the margins; every image pixel is written by the composition loop below.
            for (int y = 0; y < height; ++y)
            {
                auto* row = pixels.get() + static_cast<size_t>(y) * width * 4U;
                const bool imageRow = y >= offsetY && y < offsetY + scaledHeight;
                const int left = imageRow ? offsetX : width;
                for (int x = 0; x < left; ++x)
                    std::memcpy(row + static_cast<size_t>(x) * 4U, backgroundPixel.data(), 4U);
                if (imageRow)
                    for (int x = offsetX + scaledWidth; x < width; ++x)
                        std::memcpy(row + static_cast<size_t>(x) * 4U, backgroundPixel.data(), 4U);
            }

            struct HorizontalSample
            {
                size_t left;
                size_t right;
                double weight;
            };
            std::unique_ptr<HorizontalSample[]> horizontal;
            if (scale != 1.0)
            {
                horizontal = std::make_unique_for_overwrite<HorizontalSample[]>(scaledWidth);
                for (int x = 0; x < scaledWidth; ++x)
                {
                    const double sourceX = std::clamp((x + 0.5) / scale - 0.5, 0.0, static_cast<double>(fWidth - 1));
                    const uint32_t x0 = static_cast<uint32_t>(sourceX);
                    horizontal[x] = {static_cast<size_t>(x0) * 4U,
                                     static_cast<size_t>(std::min(x0 + 1, fWidth - 1)) * 4U, sourceX - x0};
                }
            }
            for (int y = 0; y < scaledHeight; ++y)
            {
                const double sourceY = std::clamp((y + 0.5) / scale - 0.5, 0.0, static_cast<double>(fHeight - 1));
                const uint32_t y0 = static_cast<uint32_t>(sourceY);
                const uint32_t y1 = std::min(y0 + 1, fHeight - 1);
                const double yWeight = sourceY - y0;
                const auto* topRow = reinterpret_cast<const uint8_t*>(fPixels.get()) +
                                     static_cast<size_t>(y0) * fWidth * 4U;
                const auto* bottomRow = reinterpret_cast<const uint8_t*>(fPixels.get()) +
                                        static_cast<size_t>(y1) * fWidth * 4U;
                auto* target = reinterpret_cast<uint8_t*>(pixels.get()) +
                               (static_cast<size_t>(y + offsetY) * width + offsetX) * 4U;
                for (int x = 0; x < scaledWidth; ++x, target += 4)
                {
                    std::array<uint8_t, 4> sourcePixel;
                    if (!horizontal)
                        std::memcpy(sourcePixel.data(), topRow + static_cast<size_t>(x) * 4U, 4U);
                    else
                    {
                        const auto& sample = horizontal[x];
                        for (size_t channel = 0; channel < 4; ++channel)
                        {
                            const double top = std::lerp(static_cast<double>(topRow[sample.left + channel]),
                                                         static_cast<double>(topRow[sample.right + channel]),
                                                         sample.weight);
                            const double bottom = std::lerp(static_cast<double>(bottomRow[sample.left + channel]),
                                                            static_cast<double>(bottomRow[sample.right + channel]),
                                                            sample.weight);
                            sourcePixel[channel] = static_cast<uint8_t>(std::lround(std::lerp(top, bottom, yWeight)));
                        }
                    }
                    const uint8_t inverseAlpha = 255U - sourcePixel[3];
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        target[channel] = static_cast<uint8_t>(sourcePixel[channel] +
                                                               (backgroundPixel[channel] * inverseAlpha + 127U) / 255U);
                    }
                    target[3] = static_cast<uint8_t>(sourcePixel[3] +
                                                     (backgroundPixel[3] * inverseAlpha + 127U) / 255U);
                }
            }

            return std::make_unique<Impl>(static_cast<uint32_t>(width), static_cast<uint32_t>(height),
                                          std::move(pixels));
        }

        BitmapBuffer GetBuffer() const
        {
            return {
                .pixels = {fPixels.get(), static_cast<size_t>(fWidth) * fHeight * 4U},
                .format = BitmapPixelFormat::Bgra8Premultiplied,
                .rowOrder = BitmapRowOrder::TopDown,
                .width = fWidth,
                .height = fHeight,
                .rowPitch = fWidth * 4U,
            };
        }

      private:

        uint32_t fWidth{};
        uint32_t fHeight{};
        std::unique_ptr<std::byte[]> fPixels;
    };

    Bitmap::Bitmap(const BitmapBuffer& bitmapBuffer) : impl_(std::make_unique<Impl>(bitmapBuffer)) {}
    Bitmap::Bitmap(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
    Bitmap::~Bitmap() = default;
    BitmapSharedPtr Bitmap::resize(int width, int height, LLUtils::Color background) const
    {
        return BitmapSharedPtr(new Bitmap(impl_->Resize(width, height, background)));
    }
    BitmapBuffer Bitmap::GetBuffer() const
    {
        return impl_->GetBuffer();
    }
}  // namespace LWS
