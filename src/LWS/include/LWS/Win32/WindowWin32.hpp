#pragma once
#ifdef LWS_PLATFORM_WIN32

#include <LWS/Window.hpp>

namespace LWS { class WindowBackendWin32; }

namespace LWS::Win32
{
    class WindowWin32 : public LWS::Window
    {
    public:
        WindowWin32() = default;

        void SetMenuChar(bool suppress);
        bool GetEnableMenuChar() const;
    };
}

namespace LWS { using WindowWin32 = LWS::Win32::WindowWin32; }

#endif // LWS_PLATFORM_WIN32