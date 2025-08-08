#pragma once

#include <Windows.h>

#include <cstdint>
#include <initializer_list>

namespace LWS::internal
{
    enum class WindowPosOp
    {
        None,
        Resize,
        Move,
        Zorder,
        UpdateFrame,
        Placement,
        AlwaysOnTop,
    };

    class WindowPosHelper
    {
    public:
        static constexpr UINT kBaseFlags =
            SWP_NOMOVE |
            SWP_NOSIZE |
            SWP_NOZORDER |
            SWP_NOOWNERZORDER |
            SWP_NOACTIVATE |
            SWP_NOCOPYBITS |
            SWP_NOREPOSITION |
            SWP_NOREDRAW |
            SWP_NOSENDCHANGING |
            SWP_DEFERERASE;

        static UINT flagsForOp(WindowPosOp op)
        {
            switch (op)
            {
            case WindowPosOp::None:
                return 0;
            case WindowPosOp::Placement:
                return kBaseFlags & static_cast<UINT>(~(SWP_NOMOVE | SWP_NOSIZE));
            case WindowPosOp::Move:
                return kBaseFlags & static_cast<UINT>(~SWP_NOMOVE);
            case WindowPosOp::UpdateFrame:
                return kBaseFlags | static_cast<UINT>(SWP_FRAMECHANGED);
            case WindowPosOp::Resize:
                return kBaseFlags & static_cast<UINT>(~SWP_NOSIZE);
            case WindowPosOp::Zorder:
            case WindowPosOp::AlwaysOnTop:
                return kBaseFlags & static_cast<UINT>(~SWP_NOZORDER);
            default:
                break;
            }

            return kBaseFlags;
        }

        static UINT composeFlags(std::initializer_list<WindowPosOp> flags)
        {
            UINT result = kBaseFlags;

            for (WindowPosOp op : flags)
            {
                switch (op)
                {
                case WindowPosOp::None:
                    break;
                case WindowPosOp::Placement:
                    result &= static_cast<UINT>(~(SWP_NOMOVE | SWP_NOSIZE));
                    break;
                case WindowPosOp::Move:
                    result &= static_cast<UINT>(~SWP_NOMOVE);
                    break;
                case WindowPosOp::UpdateFrame:
                    result |= static_cast<UINT>(SWP_FRAMECHANGED);
                    break;
                case WindowPosOp::Resize:
                    result &= static_cast<UINT>(~SWP_NOSIZE);
                    break;
                case WindowPosOp::AlwaysOnTop:
                case WindowPosOp::Zorder:
                    result &= static_cast<UINT>(~SWP_NOZORDER);
                    break;
                default:
                    break;
                }
            }

            return result;
        }

        static void setAlwaysOnTop(HWND handle, bool on_top)
        {
            SetWindowPos(handle, on_top ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, flagsForOp(WindowPosOp::AlwaysOnTop));
        }

        static void setPosition(HWND handle, int32_t x, int32_t y)
        {
            SetWindowPos(handle, nullptr, x, y, 0, 0, flagsForOp(WindowPosOp::Move));
        }

        static void setSize(HWND handle, int32_t width, int32_t height)
        {
            SetWindowPos(handle, nullptr, 0, 0, width, height, flagsForOp(WindowPosOp::Resize));
        }

        static void setPlacement(HWND handle, int32_t x, int32_t y, int32_t width, int32_t height)
        {
            SetWindowPos(handle, nullptr, x, y, width, height, flagsForOp(WindowPosOp::Placement));
        }

        static void updateFrame(HWND handle)
        {
            SetWindowPos(handle, nullptr, 0, 0, 0, 0, flagsForOp(WindowPosOp::UpdateFrame));
        }
    };
}
