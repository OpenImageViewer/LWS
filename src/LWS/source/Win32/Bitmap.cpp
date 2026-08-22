#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <LWS/Bitmap.hpp>
#include <LLUtils/Exception.h>
#include <LLUtils/FileHelper.h>
#include <LLUtils/PlatformUtility.h>
#include <LLUtils/Utility.h>

#include <climits>
#include <fstream>

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
            const uint32_t rowPitch = LLUtils::Utility::Align<uint32_t>(header.biBitCount * width / CHAR_BIT, sizeof(DWORD));
            const uint32_t pixelsDataSize = rowPitch * width;

            std::unique_ptr<std::uint8_t[]> emptyBuffer = std::make_unique<std::uint8_t[]>(pixelsDataSize);
            memset(emptyBuffer.get(), background, pixelsDataSize);

            BitmapBuffer buffer;
            buffer.bitsPerPixel = static_cast<uint8_t>(header.biBitCount);
            buffer.pixels = std::span<const std::byte>(reinterpret_cast<const std::byte*>(emptyBuffer.get()), pixelsDataSize);
            buffer.width = static_cast<uint32_t>(width);
            buffer.height = static_cast<uint32_t>(height);
            buffer.rowPitch = rowPitch;

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
            size_t pixelsSize = header.biWidth * header.biBitCount / CHAR_BIT * header.biHeight;
            LLUtils::Buffer pixelsData(pixelsSize);

            HDC hdc = GetDC(nullptr);
            BITMAPINFO info{};
            info.bmiHeader = header;

            int returnedLines = GetDIBits(hdc, fBitmap, 0, header.biHeight, pixelsData.data(), &info, DIB_RGB_COLORS);
            if (returnedLines != header.biHeight)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Data size mismatch");
            }

            ReleaseDC(nullptr, hdc);
            fileHeader.bfSize = static_cast<DWORD>(fileHeader.bfOffBits + pixelsSize);

            LLUtils::File::WriteAllBytes(fileName.wstring(), sizeof(BITMAPFILEHEADER), reinterpret_cast<std::byte*>(&fileHeader));
            LLUtils::File::WriteAllBytes(fileName.wstring(), sizeof(BITMAPINFOHEADER), reinterpret_cast<const std::byte*>(&header), true);
            LLUtils::File::WriteAllBytes(fileName.wstring(), pixelsSize, reinterpret_cast<const std::byte*>(pixelsData.data()), true);
        }

        BitmapBuffer GetBitmapHeader() const
        {
            const auto header = GetBitmapHeaderNative();
            BitmapBuffer result{};
            result.bitsPerPixel = static_cast<uint8_t>(header.biBitCount);
            result.width = static_cast<uint32_t>(header.biWidth);
            result.height = static_cast<uint32_t>(header.biHeight);
            result.rowPitch = LLUtils::Utility::Align<uint32_t>(header.biBitCount * result.width / CHAR_BIT, sizeof(DWORD));
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
            BITMAPINFO info{};
            info.bmiHeader.biBitCount = static_cast<WORD>(bitmapBuffer.bitsPerPixel);
            info.bmiHeader.biHeight = static_cast<LONG>(bitmapBuffer.height);
            info.bmiHeader.biWidth = static_cast<LONG>(bitmapBuffer.width);
            info.bmiHeader.biPlanes = 1;
            info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biSizeImage = bitmapBuffer.rowPitch * bitmapBuffer.height;

            void* bits = nullptr;
            HBITMAP bitmap = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
            if (bitmap == nullptr)
            {
                LL_EXCEPTION_SYSTEM_ERROR("unable to allocate bitmap");
            }

            if (SetDIBits(nullptr, bitmap, 0, bitmapBuffer.height, bitmapBuffer.pixels.data(), &info, DIB_RGB_COLORS) !=
                bitmapBuffer.height)
            {
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
