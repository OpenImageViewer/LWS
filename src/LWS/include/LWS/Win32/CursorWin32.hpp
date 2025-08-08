#pragma once
#ifdef LWS_PLATFORM_WIN32
#include <LWS/Cursor.hpp>
#include <LWS/Win32/CursorBackendWin32.hpp>

namespace LWS
{
    class CursorWin32 : public Cursor
    {
    public:
        CursorWin32() : Cursor(std::make_unique<CursorBackendWin32>()) {}
    };
}
#endif