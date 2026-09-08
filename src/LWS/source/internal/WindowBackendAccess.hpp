#pragma once

#include <LWS/Window.hpp>

#include "PlatformBackend.hpp"
#include <LWS/interfaces/backends.hpp>

#ifdef LWS_PLATFORM_WIN32
    #include <LWS/Win32/EventWin32.hpp>
#endif

namespace LWS::internal
{
    class WindowBackendAccess final
    {
      public:

        [[nodiscard]] static IWindowBackend* Get(Window& window);
        [[nodiscard]] static const IWindowBackend* Get(const Window& window);
        [[nodiscard]] static EventResponse Dispatch(Window& window, const AnyEvent& event);
#ifdef LWS_PLATFORM_WIN32
        [[nodiscard]] static Result SetPlatformCallback(Window& window, Win32::PlatformCallback callback);
        [[nodiscard]] static bool DispatchPlatform(Window& window, const Win32::PlatformEvent& event, LRESULT& result);
#endif
    };
}  // namespace LWS::internal
