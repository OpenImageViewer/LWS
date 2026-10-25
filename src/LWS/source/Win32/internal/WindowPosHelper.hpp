#pragma once

#include <Windows.h>

#include <cstdint>

namespace LWS::internal
{
    class WindowPosHelper
    {
      public:

        static constexpr UINT kBaseFlags = SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                                           SWP_NOCOPYBITS | SWP_NOREPOSITION | SWP_NOREDRAW | SWP_NOSENDCHANGING |
                                           SWP_DEFERERASE;

        // Normal placements use workspace coordinates except for tool windows.
        // Adding this offset converts them to screen coordinates; subtracting reverses it.
        static constexpr POINT workspaceOffset(const MONITORINFO& monitor, DWORD extendedStyle)
        {
            POINT offset{};
            if ((extendedStyle & WS_EX_TOOLWINDOW) == 0)
                offset = {monitor.rcWork.left - monitor.rcMonitor.left, monitor.rcWork.top - monitor.rcMonitor.top};
            return offset;
        }

        static constexpr UINT MoveFlags = kBaseFlags & ~UINT(SWP_NOMOVE);
        static constexpr UINT ResizeFlags = kBaseFlags & ~UINT(SWP_NOSIZE);
        static constexpr UINT PlacementFlags = kBaseFlags & ~UINT(SWP_NOMOVE | SWP_NOSIZE);
        static constexpr UINT FrameFlags = kBaseFlags | SWP_FRAMECHANGED;
        static constexpr UINT ZOrderFlags = kBaseFlags & ~UINT(SWP_NOZORDER);

        static void setAlwaysOnTop(HWND handle, bool on_top)
        {
            SetWindowPos(handle, on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, ZOrderFlags);
        }

        static void setPosition(HWND handle, int32_t x, int32_t y)
        {
            SetWindowPos(handle, nullptr, x, y, 0, 0, MoveFlags);
        }

        static void setSize(HWND handle, int32_t width, int32_t height)
        {
            SetWindowPos(handle, nullptr, 0, 0, width, height, ResizeFlags);
        }

        static void setPlacement(HWND handle, int32_t x, int32_t y, int32_t width, int32_t height)
        {
            SetWindowPos(handle, nullptr, x, y, width, height, PlacementFlags);
        }

        static void updateFrame(HWND handle) { SetWindowPos(handle, nullptr, 0, 0, 0, 0, FrameFlags); }
    };
}  // namespace LWS::internal
