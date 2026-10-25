#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <limits>
    #include <utility>

    #include <LWS/source/Win32/internal/CursorBackendWin32.hpp>
    #include <LWS/source/internal/BitmapPixels.hpp>
    #include "internal/BgraDib.hpp"

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
        fVisible = visible;
        SetCursor(getCursorHandle());
    }

    void CursorBackendWin32::setCursorShape(CursorShape shape)
    {
        const HCURSOR previous = std::exchange(fCustomCursor, nullptr);
        fCurrentCursor = loadCursorShape(shape);
        SetCursor(getCursorHandle());
        if (previous != nullptr)
            DestroyCursor(previous);
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

        void* pixels = nullptr;
        HBITMAP color_bitmap = internal::createBgraDib(bmp.width, bmp.height, pixels);

        if (color_bitmap == nullptr || pixels == nullptr)
        {
            if (color_bitmap != nullptr)
            {
                DeleteObject(color_bitmap);
            }
            return Result::Failure;
        }

        internal::copyBitmapPixels(bmp, *layout, static_cast<std::byte*>(pixels));

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
        if (!fVisible)
            return nullptr;
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
