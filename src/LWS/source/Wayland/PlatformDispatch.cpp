// Platform dispatch for Wayland: creates Wayland backend instances.
// Compiled only when LWS_PLATFORM_WAYLAND is defined.
// Null fallback for all other non-Win32 platforms is also provided here.
#include <LWS/interfaces/backends.hpp>

#ifdef LWS_PLATFORM_WAYLAND
#include <LWS/Wayland/CursorBackendWayland.hpp>
#include <LWS/Wayland/WindowBackendWayland.hpp>

namespace LWS::internal
{
    std::unique_ptr<IWindowBackend> createDefaultWindowBackend()
    {
        return std::make_unique<WindowBackendWayland>();
    }

    std::unique_ptr<ICursorBackend> createDefaultCursorBackend()
    {
        return std::make_unique<CursorBackendWayland>();
    }
}
#elif !defined(LWS_PLATFORM_WIN32)
// Null fallback for any platform without a concrete backend (e.g. X11 scaffold, headless).
namespace LWS::internal
{
    std::unique_ptr<IWindowBackend> createDefaultWindowBackend() { return nullptr; }
    std::unique_ptr<ICursorBackend> createDefaultCursorBackend() { return nullptr; }
}
#endif
