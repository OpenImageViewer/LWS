#pragma once

#include <LWS/Window.hpp>

#include "PlatformBackend.hpp"
#include <LWS/source/internal/Backends.hpp>

#ifdef LWS_PLATFORM_WIN32
    #include <LWS/Win32/EventWin32.hpp>
#endif

namespace LWS::internal
{
    class WindowBackendAccess final
    {
      public:

        // Covers native frames as well as portable dispatch; release builds retain no owner tracking.
        class DispatchScope final
        {
          public:

            explicit DispatchScope(Window& window)
#ifndef NDEBUG
                : window_(window)
#endif
            {
#ifndef NDEBUG
                ++window_.dispatchDepth_;
#else
                (void) window;
#endif
            }
            ~DispatchScope()
            {
#ifndef NDEBUG
                --window_.dispatchDepth_;
#endif
            }
            DispatchScope(const DispatchScope&) = delete;
            DispatchScope& operator=(const DispatchScope&) = delete;

          private:

#ifndef NDEBUG
            Window& window_;
#endif
        };
        [[nodiscard]] static bool HasNativeHandle(const Window& window);
        [[nodiscard]] static bool CanConfigure(const Window& window);
        [[nodiscard]] static IWindowBackend* Get(Window& window);
        [[nodiscard]] static const IWindowBackend* Get(const Window& window);
        [[nodiscard]] static EventResponse Dispatch(Window& window, const AnyEvent& event);
#ifdef LWS_PLATFORM_WIN32
        [[nodiscard]] static std::expected<EventConnection, Result> ListenPlatform(Window& window,
                                                                                   Win32::PlatformCallback callback);
        [[nodiscard]] static bool DispatchPlatform(Window& window, const Win32::PlatformEvent& event, LRESULT& result);
#endif
    };
}  // namespace LWS::internal
