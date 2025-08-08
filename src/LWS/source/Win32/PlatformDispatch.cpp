#include <LWS/interfaces/backends.hpp>

#ifdef LWS_PLATFORM_WIN32
#include <LWS/Win32/CursorBackendWin32.hpp>
#include <LWS/Win32/WindowBackendWin32.hpp>

namespace LWS::internal
{
    std::unique_ptr<IWindowBackend> createDefaultWindowBackend()
    {
        return std::make_unique<WindowBackendWin32>();
    }

    std::unique_ptr<ICursorBackend> createDefaultCursorBackend()
    {
        return std::make_unique<CursorBackendWin32>();
    }
}
#endif // LWS_PLATFORM_WIN32
