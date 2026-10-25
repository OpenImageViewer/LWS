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
    [[nodiscard]] std::expected<HWND, Result> GetHwnd(const Window& window);
    [[nodiscard]] std::expected<EventConnection, Result> Listen(Window& window, PlatformCallback callback);
    [[nodiscard]] Result SetMenuChar(Window& window, bool suppress);
    /// Context-thread only; requires a created window. False releases only this window's capture.
    /// Capture loss is reported through EventMouseCaptureLost. Native callbacks may run synchronously.
    [[nodiscard]] Result SetMouseCapture(Window& window, bool capture);
}  // namespace LWS::Win32

#endif  // LWS_HAS_WIN32_BACKEND
