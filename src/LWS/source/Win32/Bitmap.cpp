#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <LWS/Bitmap.hpp>
#include <LLUtils/Exception.h>
#include <LLUtils/FileHelper.h>

#include <limits>

namespace
{
    struct BitmapLayout
    {
        uint32_t rowPitch;
        DWORD pixelSize;
        uint32_t height;
    };

    BitmapLayout GetBitmapLayout(int64_t width, int64_t height, uint16_t bitsPerPixel)
    {
        if (width <= 0 || height <= 0 || bitsPerPixel == 0 ||
            width > std::numeric_limits<LONG>::max() || height > std::numeric_limits<LONG>::max())
        {
            LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Invalid bitmap layout");
        }

        const uint64_t rowPitch = ((static_cast<uint64_t>(width) * bitsPerPixel + 31U) / 32U) * sizeof(DWORD);
        const uint64_t pixelSize = rowPitch * static_cast<uint64_t>(height);
        if (pixelSize > std::numeric_limits<DWORD>::max())
        {
            LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Bitmap pixel buffer exceeds Win32 limits");
        }

        return {
            static_cast<uint32_t>(rowPitch),
            static_cast<DWORD>(pixelSize),
            static_cast<uint32_t>(height),
        };
    }

    BitmapLayout GetBitmapLayout(const BITMAPINFOHEADER& header)
    {
        const int64_t height = header.biHeight < 0 ? -static_cast<int64_t>(header.biHeight) : header.biHeight;
        return GetBitmapLayout(header.biWidth, height, header.biBitCount);
    }
}

namespace LWS
{
    class Bitmap::Impl
    {
    public:
        explicit Impl(const BitmapBuffer& bitmapBuffer)
        {
            fBitmap = FromMemory(bitmapBuffer);
        }

        explicit Impl(const std::filesystem::path& fileName)
        {
            fBitmap = FromFileAnyFormat(fileName);
        }

        ~Impl()
        {
            if (fBitmap != nullptr)
            {
                DeleteObject(fBitmap);
            }
        }

        BitmapSharedPtr Resize(int width, int height, uint8_t background) const
        {
            HDC dcSrc = CreateCompatibleDC(nullptr);
            SelectObject(dcSrc, fBitmap);

            const auto header = GetBitmapHeaderNative();
            const BitmapLayout layout = GetBitmapLayout(width, height, header.biBitCount);

            std::unique_ptr<std::uint8_t[]> emptyBuffer = std::make_unique<std::uint8_t[]>(layout.pixelSize);
            memset(emptyBuffer.get(), background, layout.pixelSize);

            BitmapBuffer buffer;
            buffer.bitsPerPixel = static_cast<uint8_t>(header.biBitCount);
            buffer.pixels = std::span<const std::byte>(reinterpret_cast<const std::byte*>(emptyBuffer.get()), layout.pixelSize);
            buffer.width = static_cast<uint32_t>(width);
            buffer.height = static_cast<uint32_t>(height);
            buffer.rowPitch = layout.rowPitch;

            BitmapSharedPtr resized = std::make_shared<Bitmap>(buffer);
            HDC dst = CreateCompatibleDC(nullptr);
            SelectObject(dst, reinterpret_cast<HBITMAP>(resized->GetNativeHandle()));
            SetStretchBltMode(dst, STRETCH_HALFTONE);

            size_t finalWidth = std::min<size_t>(width, static_cast<size_t>(header.biWidth));
            size_t finalHeight = std::min<size_t>(height, static_cast<size_t>(header.biHeight));
            size_t posX = (width - finalWidth) / 2;
            size_t posY = (height - finalHeight) / 2;

            StretchBlt(dst, static_cast<int>(posX), static_cast<int>(posY), static_cast<int>(finalWidth),
                       static_cast<int>(finalHeight), dcSrc, 0, 0, header.biWidth, header.biHeight, SRCCOPY);

            DeleteDC(dcSrc);
            DeleteDC(dst);

            return resized;
        }

        void SaveToFile(const std::filesystem::path& fileName) const
        {
            BITMAPFILEHEADER fileHeader{};
            fileHeader.bfType = 0x4D42;
            fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);

            const auto header = GetBitmapHeaderNative();
            const BitmapLayout layout = GetBitmapLayout(header);
            LLUtils::Buffer pixelsData(layout.pixelSize);

            HDC hdc = GetDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader = header;

            int returnedLines = GetDIBits(hdc, fBitmap, 0, layout.height, pixelsData.data(), &info, DIB_RGB_COLORS);
            ReleaseDC(nullptr, hdc);
            if (returnedLines != static_cast<int>(layout.height))
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Data size mismatch");
            }

            if (layout.pixelSize > std::numeric_limits<DWORD>::max() - fileHeader.bfOffBits)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Bitmap is too large for the BMP file format");
            }
            fileHeader.bfSize = fileHeader.bfOffBits + layout.pixelSize;

            LLUtils::File::WriteAllBytes(fileName.wstring(), sizeof(BITMAPFILEHEADER), reinterpret_cast<std::byte*>(&fileHeader));
            LLUtils::File::WriteAllBytes(fileName.wstring(), sizeof(BITMAPINFOHEADER), reinterpret_cast<const std::byte*>(&header), true);
            LLUtils::File::WriteAllBytes(fileName.wstring(), layout.pixelSize, reinterpret_cast<const std::byte*>(pixelsData.data()), true);
        }

        BitmapBuffer GetBitmapHeader() const
        {
            const auto header = GetBitmapHeaderNative();
            const BitmapLayout layout = GetBitmapLayout(header);
            BitmapBuffer result{};
            result.bitsPerPixel = static_cast<uint8_t>(header.biBitCount);
            result.width = static_cast<uint32_t>(header.biWidth);
            result.height = layout.height;
            result.rowPitch = layout.rowPitch;
            return result;
        }

        Handle GetNativeHandle() const
        {
            return reinterpret_cast<Handle>(fBitmap);
        }

    private:
        BITMAPINFOHEADER GetBitmapHeaderNative() const
        {
            if (fBitmapInfo.bmiHeader.biSize == 0)
            {
                fBitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
                HDC hdc = GetDC(nullptr);
                GetDIBits(hdc, fBitmap, 0, 1, nullptr, reinterpret_cast<BITMAPINFO*>(&fBitmapInfo), DIB_RGB_COLORS);
                ReleaseDC(nullptr, hdc);
            }
            return fBitmapInfo.bmiHeader;
        }

        static HBITMAP FromMemory(const BitmapBuffer& bitmapBuffer)
        {
            const BitmapLayout layout = GetBitmapLayout(bitmapBuffer.width, bitmapBuffer.height, bitmapBuffer.bitsPerPixel);
            if (bitmapBuffer.rowPitch != layout.rowPitch || bitmapBuffer.pixels.size() < layout.pixelSize)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::BadParameters, "Bitmap buffer layout is invalid");
            }

            BITMAPINFO info{};
            info.bmiHeader.biBitCount = static_cast<WORD>(bitmapBuffer.bitsPerPixel);
            info.bmiHeader.biHeight = static_cast<LONG>(bitmapBuffer.height);
            info.bmiHeader.biWidth = static_cast<LONG>(bitmapBuffer.width);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biSizeImage = layout.pixelSize;

            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap == nullptr)
            {
                LL_EXCEPTION_SYSTEM_ERROR("unable to allocate bitmap");
            }

            if (SetDIBits(nullptr, bitmap, 0, bitmapBuffer.height, bitmapBuffer.pixels.data(), &info, DIB_RGB_COLORS) !=
                bitmapBuffer.height)
            {
                DeleteObject(bitmap);
                LL_EXCEPTION_SYSTEM_ERROR("can not set bitmap pixels");
            }

            return bitmap;
        }

        static HBITMAP FromFileAnyFormat(const std::filesystem::path& filePath)
        {
            return static_cast<HBITMAP>(LoadImage(GetModuleHandle(nullptr), filePath.c_str(), IMAGE_BITMAP, 0, 0,
                                                  LR_LOADFROMFILE));
        }

        struct BitmapInfoCache
        {
            BITMAPINFOHEADER bmiHeader;
            RGBQUAD bmiColors[256];
        };

        mutable BitmapInfoCache fBitmapInfo = {};
        HBITMAP fBitmap = nullptr;
    };

    Bitmap::Bitmap(const BitmapBuffer& bitmapBuffer) : impl_(std::make_unique<Impl>(bitmapBuffer)) {}
    Bitmap::Bitmap(const std::filesystem::path& fileName) : impl_(std::make_unique<Impl>(fileName)) {}
    Bitmap::~Bitmap() = default;
    BitmapSharedPtr Bitmap::resize(int width, int height, uint8_t background) const { return impl_->Resize(width, height, background); }
    void Bitmap::SaveToFile(const std::filesystem::path& fileName) const { impl_->SaveToFile(fileName); }
    BitmapBuffer Bitmap::GetBitmapHeader() const { return impl_->GetBitmapHeader(); }
    Handle Bitmap::GetNativeHandle() const { return impl_->GetNativeHandle(); }
}
#endif
