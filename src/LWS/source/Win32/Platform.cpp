#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Ole2.h>
#include <ShlObj.h>

#include <LWS/Platform.hpp>
#include "internal/MonitorInfo.hpp"
#include "internal/PlatformState.hpp"

#include <mutex>

namespace
{
    struct PlatformThreadState
    {
        ~PlatformThreadState() { releaseOleReference(); }

        void releaseOleReference()
        {
            if (ownsOleReference)
                OleUninitialize();
            ownsOleReference = false;
            oleInitialized = false;
        }

        uint32_t initCount = 0;
        bool ownsOleReference = false;
        bool oleInitialized = false;
    };

    thread_local PlatformThreadState g_platformThreadState;
    std::once_flag g_processInitFlag;

    void initializeProcess()
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 == nullptr)
            return;

        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        auto setDpiAwarenessContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setDpiAwarenessContext == nullptr ||
            setDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE)
        {
            SetProcessDPIAware();
        }
    }

    int virtualKeyFromKeyCode(LWS::KeyCode key)
    {
        switch (key)
        {
        case LWS::KeyCode::A: return 'A';
        case LWS::KeyCode::B: return 'B';
        case LWS::KeyCode::C: return 'C';
        case LWS::KeyCode::D: return 'D';
        case LWS::KeyCode::E: return 'E';
        case LWS::KeyCode::F: return 'F';
        case LWS::KeyCode::G: return 'G';
        case LWS::KeyCode::H: return 'H';
        case LWS::KeyCode::I: return 'I';
        case LWS::KeyCode::J: return 'J';
        case LWS::KeyCode::K: return 'K';
        case LWS::KeyCode::L: return 'L';
        case LWS::KeyCode::M: return 'M';
        case LWS::KeyCode::N: return 'N';
        case LWS::KeyCode::O: return 'O';
        case LWS::KeyCode::P: return 'P';
        case LWS::KeyCode::Q: return 'Q';
        case LWS::KeyCode::R: return 'R';
        case LWS::KeyCode::S: return 'S';
        case LWS::KeyCode::T: return 'T';
        case LWS::KeyCode::U: return 'U';
        case LWS::KeyCode::V: return 'V';
        case LWS::KeyCode::W: return 'W';
        case LWS::KeyCode::X: return 'X';
        case LWS::KeyCode::Y: return 'Y';
        case LWS::KeyCode::Z: return 'Z';
        case LWS::KeyCode::Digit0: return '0';
        case LWS::KeyCode::Digit1: return '1';
        case LWS::KeyCode::Digit2: return '2';
        case LWS::KeyCode::Digit3: return '3';
        case LWS::KeyCode::Digit4: return '4';
        case LWS::KeyCode::Digit5: return '5';
        case LWS::KeyCode::Digit6: return '6';
        case LWS::KeyCode::Digit7: return '7';
        case LWS::KeyCode::Digit8: return '8';
        case LWS::KeyCode::Digit9: return '9';
        case LWS::KeyCode::F1: return VK_F1;
        case LWS::KeyCode::F2: return VK_F2;
        case LWS::KeyCode::F3: return VK_F3;
        case LWS::KeyCode::F4: return VK_F4;
        case LWS::KeyCode::F5: return VK_F5;
        case LWS::KeyCode::F6: return VK_F6;
        case LWS::KeyCode::F7: return VK_F7;
        case LWS::KeyCode::F8: return VK_F8;
        case LWS::KeyCode::F9: return VK_F9;
        case LWS::KeyCode::F10: return VK_F10;
        case LWS::KeyCode::F11: return VK_F11;
        case LWS::KeyCode::F12: return VK_F12;
        case LWS::KeyCode::Left: return VK_LEFT;
        case LWS::KeyCode::Right: return VK_RIGHT;
        case LWS::KeyCode::Up: return VK_UP;
        case LWS::KeyCode::Down: return VK_DOWN;
        case LWS::KeyCode::Home: return VK_HOME;
        case LWS::KeyCode::End: return VK_END;
        case LWS::KeyCode::PageUp: return VK_PRIOR;
        case LWS::KeyCode::PageDown: return VK_NEXT;
        case LWS::KeyCode::Insert: return VK_INSERT;
        case LWS::KeyCode::Delete: return VK_DELETE;
        case LWS::KeyCode::Enter: return VK_RETURN;
        case LWS::KeyCode::Escape: return VK_ESCAPE;
        case LWS::KeyCode::Tab: return VK_TAB;
        case LWS::KeyCode::Backspace: return VK_BACK;
        case LWS::KeyCode::Space: return VK_SPACE;
        case LWS::KeyCode::Shift: return VK_SHIFT;
        case LWS::KeyCode::Control: return VK_CONTROL;
        case LWS::KeyCode::Alt: return VK_MENU;
        case LWS::KeyCode::Win: return VK_LWIN;
        case LWS::KeyCode::LShift: return VK_LSHIFT;
        case LWS::KeyCode::RShift: return VK_RSHIFT;
        case LWS::KeyCode::LControl: return VK_LCONTROL;
        case LWS::KeyCode::RControl: return VK_RCONTROL;
        case LWS::KeyCode::LAlt: return VK_LMENU;
        case LWS::KeyCode::RAlt: return VK_RMENU;
        case LWS::KeyCode::CapsLock: return VK_CAPITAL;
        case LWS::KeyCode::NumLock: return VK_NUMLOCK;
        case LWS::KeyCode::ScrollLock: return VK_SCROLL;
        case LWS::KeyCode::Numpad0: return VK_NUMPAD0;
        case LWS::KeyCode::Numpad1: return VK_NUMPAD1;
        case LWS::KeyCode::Numpad2: return VK_NUMPAD2;
        case LWS::KeyCode::Numpad3: return VK_NUMPAD3;
        case LWS::KeyCode::Numpad4: return VK_NUMPAD4;
        case LWS::KeyCode::Numpad5: return VK_NUMPAD5;
        case LWS::KeyCode::Numpad6: return VK_NUMPAD6;
        case LWS::KeyCode::Numpad7: return VK_NUMPAD7;
        case LWS::KeyCode::Numpad8: return VK_NUMPAD8;
        case LWS::KeyCode::Numpad9: return VK_NUMPAD9;
        case LWS::KeyCode::NumpadAdd: return VK_ADD;
        case LWS::KeyCode::NumpadSubtract: return VK_SUBTRACT;
        case LWS::KeyCode::NumpadMultiply: return VK_MULTIPLY;
        case LWS::KeyCode::NumpadDivide: return VK_DIVIDE;
        case LWS::KeyCode::NumpadDecimal: return VK_DECIMAL;
        case LWS::KeyCode::NumpadEnter: return VK_RETURN;
        case LWS::KeyCode::Comma: return VK_OEM_COMMA;
        case LWS::KeyCode::Period: return VK_OEM_PERIOD;
        case LWS::KeyCode::Slash: return VK_OEM_2;
        case LWS::KeyCode::Semicolon: return VK_OEM_1;
        case LWS::KeyCode::Quote: return VK_OEM_7;
        case LWS::KeyCode::LeftBracket: return VK_OEM_4;
        case LWS::KeyCode::RightBracket: return VK_OEM_6;
        case LWS::KeyCode::Backslash: return VK_OEM_5;
        case LWS::KeyCode::Minus: return VK_OEM_MINUS;
        case LWS::KeyCode::Equals: return VK_OEM_PLUS;
        case LWS::KeyCode::Tilde: return VK_OEM_3;
        case LWS::KeyCode::PrintScreen: return VK_SNAPSHOT;
        case LWS::KeyCode::Pause: return VK_PAUSE;
        case LWS::KeyCode::LButton: return VK_LBUTTON;
        case LWS::KeyCode::RButton: return VK_RBUTTON;
        case LWS::KeyCode::MButton: return VK_MBUTTON;
        default: return 0;
        }
    }

    LWS::Rect rectFromWin32(const RECT& rect)
    {
        return { { rect.left, rect.top }, { rect.right, rect.bottom } };
    }

    LWS::Platform::MonitorDesc monitorDescFromWin32(const LWS::internal::MonitorDesc& desc)
    {
        return {
            .handle = reinterpret_cast<LWS::Handle>(desc.handle),
            .deviceName = desc.displayInfo.DeviceName,
            .monitorRect = rectFromWin32(desc.monitorInfo.rcMonitor),
            .workRect = rectFromWin32(desc.monitorInfo.rcWork),
            .dpiX = desc.dpiX,
            .dpiY = desc.dpiY,
            .displayFrequency = desc.displaySettings.dmDisplayFrequency,
            .primary = (desc.monitorInfo.dwFlags & MONITORINFOF_PRIMARY) != 0,
        };
    }
}

namespace LWS::internal
{
    bool isOleInitializedForCurrentThread()
    {
        return g_platformThreadState.initCount != 0 && g_platformThreadState.oleInitialized;
    }
}  // namespace LWS::internal

namespace LWS::Platform
{
    Result init()
    {
        if (g_platformThreadState.initCount != 0)
        {
            ++g_platformThreadState.initCount;
            return Result::Success;
        }

        std::call_once(g_processInitFlag, initializeProcess);

        const HRESULT oleResult = OleInitialize(nullptr);
        if (oleResult == S_OK || oleResult == S_FALSE)
        {
            g_platformThreadState.ownsOleReference = true;
            g_platformThreadState.oleInitialized = true;
        }
        else if (oleResult != RPC_E_CHANGED_MODE)
        {
            return Result::Failure;
        }

        g_platformThreadState.initCount = 1;
        return Result::Success;
    }

    void shutdown()
    {
        if (g_platformThreadState.initCount == 0)
        {
            return;
        }

        --g_platformThreadState.initCount;
        if (g_platformThreadState.initCount == 0)
        {
            g_platformThreadState.releaseOleReference();
        }
    }

    bool isInitialized()
    {
        return g_platformThreadState.initCount != 0;
    }

    void runMessageLoop()
    {
        MSG msg{};
        while (true)
        {
            BOOL result = GetMessage(&msg, nullptr, 0, 0);
            if (result <= 0)
            {
                break;
            }

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    bool processMessages()
    {
        MSG msg{};
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
            {
                return true;
            }
        }

        return false;
    }

    bool isKeyPressed(KeyCode key)
    {
        if (key == KeyCode::Shift)
        {
            return isKeyPressed(KeyCode::LShift) || isKeyPressed(KeyCode::RShift);
        }
        if (key == KeyCode::Control)
        {
            return isKeyPressed(KeyCode::LControl) || isKeyPressed(KeyCode::RControl);
        }
        if (key == KeyCode::Alt)
        {
            return isKeyPressed(KeyCode::LAlt) || isKeyPressed(KeyCode::RAlt);
        }

        int virtualKey = virtualKeyFromKeyCode(key);
        return virtualKey != 0 && (GetKeyState(virtualKey) & 0x8000) != 0;
    }

    bool isKeyToggled(KeyCode key)
    {
        int virtualKey = virtualKeyFromKeyCode(key);
        return virtualKey != 0 && (GetKeyState(virtualKey) & 0x0001) != 0;
    }

    Point getMousePosition()
    {
        POINT point{};
        GetCursorPos(&point);
        return { point.x, point.y };
    }

    void moveMouse(Point delta)
    {
        POINT point{};
        GetCursorPos(&point);
        SetCursorPos(point.x + delta.x, point.y + delta.y);
    }

    void browseToFile(const std::filesystem::path& path)
    {
        PIDLIST_ABSOLUTE itemIdList = ILCreateFromPathW(path.c_str());
        if (itemIdList != nullptr)
        {
            SHOpenFolderAndSelectItems(itemIdList, 0, nullptr, 0);
            ILFree(itemIdList);
        }
    }

    void refreshMonitors()
    {
        internal::MonitorInfo::instance().refresh();
    }

    MonitorDesc getMonitorInfo(Handle monitorHandle, bool allowRefresh)
    {
        return monitorDescFromWin32(
            internal::MonitorInfo::instance().getMonitorInfo(reinterpret_cast<HMONITOR>(monitorHandle), allowRefresh));
    }

    MonitorDesc getPrimaryMonitor(bool allowRefresh)
    {
        return monitorDescFromWin32(internal::MonitorInfo::instance().getPrimaryMonitor(allowRefresh));
    }

    Rect getBoundingMonitorArea()
    {
        return rectFromWin32(internal::MonitorInfo::instance().getBoundingMonitorArea());
    }
}
#endif
