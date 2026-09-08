#pragma once

#include <LLUtils/Color.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace LWS
{
    enum class BitmapPixelFormat
    {
        Bgr8,
        Bgra8,
        Bgra8Premultiplied
    };

    enum class BitmapRowOrder
    {
        TopDown,
        BottomUp
    };

    struct BitmapBuffer
    {
        std::span<const std::byte> pixels;
        BitmapPixelFormat format = BitmapPixelFormat::Bgra8Premultiplied;
        BitmapRowOrder rowOrder = BitmapRowOrder::TopDown;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitch = 0;
    };

    class Bitmap;
    using BitmapSharedPtr = std::shared_ptr<Bitmap>;

    class Bitmap
    {
      public:

        explicit Bitmap(const BitmapBuffer& bitmapBuffer);
        ~Bitmap();

        Bitmap(const Bitmap&) = delete;
        Bitmap& operator=(const Bitmap&) = delete;
        Bitmap(Bitmap&&) noexcept = delete;
        Bitmap& operator=(Bitmap&&) noexcept = delete;

        [[nodiscard]] BitmapSharedPtr resize(int width, int height, LLUtils::Color background = {0, 0, 0, 0}) const;
        // The returned pixel view remains valid for this immutable bitmap's lifetime.
        [[nodiscard]] BitmapBuffer GetBuffer() const;

      private:

        class Impl;
        explicit Bitmap(std::unique_ptr<Impl> impl);
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
