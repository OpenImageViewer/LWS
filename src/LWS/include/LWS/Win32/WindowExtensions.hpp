#pragma once
#ifdef LWS_HAS_WIN32_BACKEND

    #include <LWS/Result.hpp>
    #include <LWS/Win32/EventWin32.hpp>

    #include <expected>

namespace LWS
{
    class Window;
}

namespace LWS::Win32
{
    [[nodiscard]] std::expected<HWND, Result> GetHwnd(Window& window);
    [[nodiscard]] std::expected<HWND, Result> GetHwnd(const Window& window);
    [[nodiscard]] std::expected<EventConnection, Result> Listen(Window& window, PlatformCallback callback);
    [[nodiscard]] Result SetMenuChar(Window& window, bool suppress);
}  // namespace LWS::Win32

#endif  // LWS_HAS_WIN32_BACKEND
