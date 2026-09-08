#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <cstring>
    #include <limits>

    #include <LWS/Win32/CursorBackendWin32.hpp>
    #include <LWS/source/internal/BitmapValidation.hpp>

namespace
{
    static HCURSOR loadCursorShape(LWS::CursorShape shape)
    {
        switch (shape)
        {
            case LWS::CursorShape::Arrow:
                return LoadCursor(nullptr, IDC_ARROW);
            case LWS::CursorShape::Hand:
                return LoadCursor(nullptr, IDC_HAND);
            case LWS::CursorShape::IBeam:
                return LoadCursor(nullptr, IDC_IBEAM);
            case LWS::CursorShape::SizeNS:
                return LoadCursor(nullptr, IDC_SIZENS);
            case LWS::CursorShape::SizeEW:
                return LoadCursor(nullptr, IDC_SIZEWE);
            case LWS::CursorShape::SizeNWSE:
                return LoadCursor(nullptr, IDC_SIZENWSE);
            case LWS::CursorShape::SizeNESW:
                return LoadCursor(nullptr, IDC_SIZENESW);
            case LWS::CursorShape::SizeAll:
                return LoadCursor(nullptr, IDC_SIZEALL);
            case LWS::CursorShape::No:
                return LoadCursor(nullptr, IDC_NO);
            case LWS::CursorShape::Wait:
                return LoadCursor(nullptr, IDC_WAIT);
            case LWS::CursorShape::AppStarting:
                return LoadCursor(nullptr, IDC_APPSTARTING);
            default:
                return LoadCursor(nullptr, IDC_ARROW);
        }
    }
}  // namespace

namespace LWS
{
    CursorBackendWin32::~CursorBackendWin32()
    {
        if (fCustomCursor != nullptr)
        {
            DestroyCursor(fCustomCursor);
            fCustomCursor = nullptr;
        }
    }

    void CursorBackendWin32::setVisible(bool visible)
    {
        if (fVisible == visible)
        {
            return;
        }

        fVisible = visible;
        if (visible)
        {
            while (ShowCursor(TRUE) < 0)
            {
            }
        }
        else
        {
            while (ShowCursor(FALSE) >= 0)
            {
            }
        }
    }

    void CursorBackendWin32::setCursorShape(CursorShape shape)
    {
        fCurrentCursor = loadCursorShape(shape);
        SetCursor(getCursorHandle());
    }

    Result CursorBackendWin32::setCustomCursor(const BitmapBuffer& bmp, Point hotspot)
    {
        const auto layout = internal::validateBitmapBuffer(bmp);
        if (!layout.has_value() ||
            (bmp.format != BitmapPixelFormat::Bgra8 && bmp.format != BitmapPixelFormat::Bgra8Premultiplied) ||
            bmp.width > static_cast<uint32_t>(std::numeric_limits<LONG>::max()) ||
            bmp.height > static_cast<uint32_t>(std::numeric_limits<LONG>::max()))
        {
            return Result::Failure;
        }

        const size_t dest_row_pitch = static_cast<size_t>(bmp.width) * 4U;
        const size_t source_row_pitch = layout->rowPitch;

        BITMAPV5HEADER bitmap_info{};
        bitmap_info.bV5Size = sizeof(bitmap_info);
        bitmap_info.bV5Width = static_cast<LONG>(bmp.width);
        bitmap_info.bV5Height = -static_cast<LONG>(bmp.height);
        bitmap_info.bV5Planes = 1;
        bitmap_info.bV5BitCount = 32;
        bitmap_info.bV5Compression = BI_BITFIELDS;
        bitmap_info.bV5RedMask = 0x00FF0000;
        bitmap_info.bV5GreenMask = 0x0000FF00;
        bitmap_info.bV5BlueMask = 0x000000FF;
        bitmap_info.bV5AlphaMask = 0xFF000000;

        HDC screen_dc = GetDC(nullptr);
        void* pixels = nullptr;
        HBITMAP color_bitmap = CreateDIBSection(screen_dc, reinterpret_cast<BITMAPINFO*>(&bitmap_info), DIB_RGB_COLORS,
                                                &pixels, nullptr, 0);
        ReleaseDC(nullptr, screen_dc);

        if (color_bitmap == nullptr || pixels == nullptr)
        {
            if (color_bitmap != nullptr)
            {
                DeleteObject(color_bitmap);
            }
            return Result::Failure;
        }

        const std::byte* source = bmp.pixels.data();
        std::byte* destination = static_cast<std::byte*>(pixels);
        for (uint32_t row = 0; row < bmp.height; ++row)
        {
            const uint32_t source_row = bmp.rowOrder == BitmapRowOrder::TopDown ? row : bmp.height - row - 1;
            const std::byte* source_row_pixels = source + static_cast<size_t>(source_row) * source_row_pitch;
            std::byte* destination_row_pixels = destination + static_cast<size_t>(row) * dest_row_pitch;
            if (bmp.format == BitmapPixelFormat::Bgra8Premultiplied)
            {
                std::memcpy(destination_row_pixels, source_row_pixels, dest_row_pitch);
            }
            else
            {
                const auto* source_channels = reinterpret_cast<const uint8_t*>(source_row_pixels);
                auto* destination_channels = reinterpret_cast<uint8_t*>(destination_row_pixels);
                for (uint32_t column = 0; column < bmp.width; ++column)
                {
                    const size_t offset = static_cast<size_t>(column) * 4U;
                    const uint8_t alpha = source_channels[offset + 3];
                    for (size_t channel = 0; channel < 3; ++channel)
                    {
                        destination_channels[offset + channel] = static_cast<uint8_t>(
                            (static_cast<uint16_t>(source_channels[offset + channel]) * alpha + 127U) / 255U);
                    }
                    destination_channels[offset + 3] = alpha;
                }
            }
        }

        HBITMAP mask_bitmap = CreateBitmap(static_cast<int>(bmp.width), static_cast<int>(bmp.height), 1, 1, nullptr);
        if (mask_bitmap == nullptr)
        {
            DeleteObject(color_bitmap);
            return Result::Failure;
        }

        ICONINFO icon_info{};
        icon_info.fIcon = FALSE;
        icon_info.xHotspot = static_cast<DWORD>(hotspot.x);
        icon_info.yHotspot = static_cast<DWORD>(hotspot.y);
        icon_info.hbmMask = mask_bitmap;
        icon_info.hbmColor = color_bitmap;

        HCURSOR custom_cursor = CreateIconIndirect(&icon_info);
        DeleteObject(color_bitmap);
        DeleteObject(mask_bitmap);

        if (custom_cursor == nullptr)
        {
            return Result::Failure;
        }

        if (fCustomCursor != nullptr)
        {
            DestroyCursor(fCustomCursor);
        }

        fCustomCursor = custom_cursor;
        SetCursor(getCursorHandle());
        return Result::Success;
    }

    BackendId CursorBackendWin32::backend() const
    {
        return BackendId::Win32;
    }

    HCURSOR CursorBackendWin32::getCursorHandle() const
    {
        if (fCustomCursor != nullptr)
        {
            return fCustomCursor;
        }

        if (fCurrentCursor != nullptr)
        {
            return fCurrentCursor;
        }

        return LoadCursor(nullptr, IDC_ARROW);
    }
}  // namespace LWS

namespace LWS::internal
{
    std::unique_ptr<ICursorBackend> createDefaultCursorBackend()
    {
        return std::make_unique<CursorBackendWin32>();
    }
}  // namespace LWS::internal
#endif
