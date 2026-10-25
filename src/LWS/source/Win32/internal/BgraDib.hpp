#pragma once
#include <Windows.h>
#include <cstdint>
namespace LWS::internal
{
    // Native callers retain ownership and their existing validation and cleanup order. This routine
    // creates the top-down DIB with explicit BGRA channel masks.
    inline HBITMAP createBgraDib(uint32_t width, uint32_t height, void*& pixels)
    {
        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = static_cast<LONG>(width);
        header.bV5Height = -static_cast<LONG>(height);
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;
        HDC screen = GetDC(nullptr);
        HBITMAP bitmap = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &pixels,
                                          nullptr, 0);
        ReleaseDC(nullptr, screen);
        return bitmap;
    }
}  // namespace LWS::internal
