#ifdef LWS_PLATFORM_WIN32
#include <LWS/Win32/WindowBackendWin32.hpp>
#include <LWS/Win32/WindowWin32.hpp>

namespace LWS::Win32
{
    void WindowWin32::SetMenuChar(bool suppress)
    {
        if (WindowBackendWin32* backend = getBackendAs<WindowBackendWin32>())
        {
            backend->setMenuChar(suppress);
        }
    }

    bool WindowWin32::GetEnableMenuChar() const
    {
        if (const WindowBackendWin32* backend = getBackendAs<WindowBackendWin32>())
        {
            return backend->getMenuChar();
        }

        return false;
    }
}
#endif
