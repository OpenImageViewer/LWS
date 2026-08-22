#pragma once

#include <LWS/interfaces/backends.hpp>

#include <memory>
#include <span>

namespace LWS
{
    class Bitmap;
    using BitmapSharedPtr = std::shared_ptr<Bitmap>;

    class Bitmap
    {
    public:
        explicit Bitmap(const BitmapBuffer& bitmapBuffer);
        explicit Bitmap(const std::filesystem::path& fileName);
        ~Bitmap();

        Bitmap(const Bitmap&) = delete;
        Bitmap& operator=(const Bitmap&) = delete;
        Bitmap(Bitmap&&) noexcept = delete;
        Bitmap& operator=(Bitmap&&) noexcept = delete;

        [[nodiscard]] BitmapSharedPtr resize(int width, int height, uint8_t background = 0) const;
        void SaveToFile(const std::filesystem::path& fileName) const;
        [[nodiscard]] BitmapBuffer GetBitmapHeader() const;
        [[nodiscard]] Handle GetNativeHandle() const;

    private:
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}
