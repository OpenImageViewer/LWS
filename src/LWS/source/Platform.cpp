#include <LWS/Platform.hpp>

#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <Ole2.h>
#include <ShlObj.h>

namespace
{
    static UINT g_platformInitCount = 0;
    static bool g_oleInitialized = false;

    static LWS::KeyCode keyCodeFromVirtualKey(WPARAM virtual_key, LPARAM lparam = 0)
    {
        switch (virtual_key)
        {
        case 'A': return LWS::KeyCode::A;
        case 'B': return LWS::KeyCode::B;
        case 'C': return LWS::KeyCode::C;
        case 'D': return LWS::KeyCode::D;
        case 'E': return LWS::KeyCode::E;
        case 'F': return LWS::KeyCode::F;
        case 'G': return LWS::KeyCode::G;
        case 'H': return LWS::KeyCode::H;
        case 'I': return LWS::KeyCode::I;
        case 'J': return LWS::KeyCode::J;
        case 'K': return LWS::KeyCode::K;
        case 'L': return LWS::KeyCode::L;
        case 'M': return LWS::KeyCode::M;
        case 'N': return LWS::KeyCode::N;
        case 'O': return LWS::KeyCode::O;
        case 'P': return LWS::KeyCode::P;
        case 'Q': return LWS::KeyCode::Q;
        case 'R': return LWS::KeyCode::R;
        case 'S': return LWS::KeyCode::S;
        case 'T': return LWS::KeyCode::T;
        case 'U': return LWS::KeyCode::U;
        case 'V': return LWS::KeyCode::V;
        case 'W': return LWS::KeyCode::W;
        case 'X': return LWS::KeyCode::X;
        case 'Y': return LWS::KeyCode::Y;
        case 'Z': return LWS::KeyCode::Z;
        case '0': return LWS::KeyCode::Digit0;
        case '1': return LWS::KeyCode::Digit1;
        case '2': return LWS::KeyCode::Digit2;
        case '3': return LWS::KeyCode::Digit3;
        case '4': return LWS::KeyCode::Digit4;
        case '5': return LWS::KeyCode::Digit5;
        case '6': return LWS::KeyCode::Digit6;
        case '7': return LWS::KeyCode::Digit7;
        case '8': return LWS::KeyCode::Digit8;
        case '9': return LWS::KeyCode::Digit9;
        case VK_F1: return LWS::KeyCode::F1;
        case VK_F2: return LWS::KeyCode::F2;
        case VK_F3: return LWS::KeyCode::F3;
        case VK_F4: return LWS::KeyCode::F4;
        case VK_F5: return LWS::KeyCode::F5;
        case VK_F6: return LWS::KeyCode::F6;
        case VK_F7: return LWS::KeyCode::F7;
        case VK_F8: return LWS::KeyCode::F8;
        case VK_F9: return LWS::KeyCode::F9;
        case VK_F10: return LWS::KeyCode::F10;
        case VK_F11: return LWS::KeyCode::F11;
        case VK_F12: return LWS::KeyCode::F12;
        case VK_LEFT: return LWS::KeyCode::Left;
        case VK_RIGHT: return LWS::KeyCode::Right;
        case VK_UP: return LWS::KeyCode::Up;
        case VK_DOWN: return LWS::KeyCode::Down;
        case VK_HOME: return LWS::KeyCode::Home;
        case VK_END: return LWS::KeyCode::End;
        case VK_PRIOR: return LWS::KeyCode::PageUp;
        case VK_NEXT: return LWS::KeyCode::PageDown;
        case VK_INSERT: return LWS::KeyCode::Insert;
        case VK_DELETE: return LWS::KeyCode::Delete;
        case VK_RETURN:
            return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::NumpadEnter : LWS::KeyCode::Enter;
        case VK_ESCAPE: return LWS::KeyCode::Escape;
        case VK_TAB: return LWS::KeyCode::Tab;
        case VK_BACK: return LWS::KeyCode::Backspace;
        case VK_SPACE: return LWS::KeyCode::Space;
        case VK_SHIFT:
        {
            UINT scancode = static_cast<UINT>((lparam >> 16) & 0xFF);
            UINT mapped = MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX);
            return mapped == VK_RSHIFT ? LWS::KeyCode::RShift : LWS::KeyCode::LShift;
        }
        case VK_CONTROL: return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::RControl : LWS::KeyCode::LControl;
        case VK_MENU: return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::RAlt : LWS::KeyCode::LAlt;
        case VK_LWIN:
        case VK_RWIN: return LWS::KeyCode::Win;
        case VK_CAPITAL: return LWS::KeyCode::CapsLock;
        case VK_NUMLOCK: return LWS::KeyCode::NumLock;
        case VK_SCROLL: return LWS::KeyCode::ScrollLock;
        case VK_NUMPAD0: return LWS::KeyCode::Numpad0;
        case VK_NUMPAD1: return LWS::KeyCode::Numpad1;
        case VK_NUMPAD2: return LWS::KeyCode::Numpad2;
        case VK_NUMPAD3: return LWS::KeyCode::Numpad3;
        case VK_NUMPAD4: return LWS::KeyCode::Numpad4;
        case VK_NUMPAD5: return LWS::KeyCode::Numpad5;
        case VK_NUMPAD6: return LWS::KeyCode::Numpad6;
        case VK_NUMPAD7: return LWS::KeyCode::Numpad7;
        case VK_NUMPAD8: return LWS::KeyCode::Numpad8;
        case VK_NUMPAD9: return LWS::KeyCode::Numpad9;
        case VK_ADD: return LWS::KeyCode::NumpadAdd;
        case VK_SUBTRACT: return LWS::KeyCode::NumpadSubtract;
        case VK_MULTIPLY: return LWS::KeyCode::NumpadMultiply;
        case VK_DIVIDE: return LWS::KeyCode::NumpadDivide;
        case VK_DECIMAL: return LWS::KeyCode::NumpadDecimal;
        case VK_OEM_COMMA: return LWS::KeyCode::Comma;
        case VK_OEM_PERIOD: return LWS::KeyCode::Period;
        case VK_OEM_2: return LWS::KeyCode::Slash;
        case VK_OEM_1: return LWS::KeyCode::Semicolon;
        case VK_OEM_7: return LWS::KeyCode::Quote;
        case VK_OEM_4: return LWS::KeyCode::LeftBracket;
        case VK_OEM_6: return LWS::KeyCode::RightBracket;
        case VK_OEM_5: return LWS::KeyCode::Backslash;
        case VK_OEM_MINUS: return LWS::KeyCode::Minus;
        case VK_OEM_PLUS: return LWS::KeyCode::Equals;
        case VK_OEM_3: return LWS::KeyCode::Tilde;
        case VK_SNAPSHOT: return LWS::KeyCode::PrintScreen;
        case VK_PAUSE: return LWS::KeyCode::Pause;
        case VK_LBUTTON: return LWS::KeyCode::LButton;
        case VK_RBUTTON: return LWS::KeyCode::RButton;
        case VK_MBUTTON: return LWS::KeyCode::MButton;
        default: return LWS::KeyCode::Unknown;
        }
    }

    static int virtualKeyFromKeyCode(LWS::KeyCode key)
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
}

namespace LWS::Platform
{
    void init()
    {
        if (g_platformInitCount++ != 0)
        {
            return;
        }

        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 != nullptr)
        {
            using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
            SetProcessDpiAwarenessContextFn set_dpi_awareness_context = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
                GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
            if (set_dpi_awareness_context != nullptr)
            {
                if (set_dpi_awareness_context(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) == FALSE)
                {
                    SetProcessDPIAware();
                }
            }
            else
            {
                SetProcessDPIAware();
            }
        }

        g_oleInitialized = SUCCEEDED(OleInitialize(nullptr));
    }

    void shutdown()
    {
        if (g_platformInitCount == 0)
        {
            return;
        }

        --g_platformInitCount;
        if (g_platformInitCount == 0 && g_oleInitialized)
        {
            OleUninitialize();
            g_oleInitialized = false;
        }
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

        int virtual_key = virtualKeyFromKeyCode(key);
        return virtual_key != 0 && (GetKeyState(virtual_key) & 0x8000) != 0;
    }

    bool isKeyToggled(KeyCode key)
    {
        int virtual_key = virtualKeyFromKeyCode(key);
        return virtual_key != 0 && (GetKeyState(virtual_key) & 0x0001) != 0;
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
        PIDLIST_ABSOLUTE item_id_list = ILCreateFromPathW(path.c_str());
        if (item_id_list != nullptr)
        {
            SHOpenFolderAndSelectItems(item_id_list, 0, nullptr, 0);
            ILFree(item_id_list);
        }
    }
}
#elif defined(LWS_PLATFORM_WAYLAND)
// ---------------------------------------------------------------------------
// Wayland platform stubs — replace with wl_display_connect + epoll event loop
// ---------------------------------------------------------------------------
namespace LWS::Platform
{
    // TODO: store wl_display* globally after wl_display_connect(nullptr)
    void init()
    {
        // TODO: wl_display_connect(nullptr) → g_wlDisplay
        // TODO: wl_display_get_registry(g_wlDisplay) → bind wl_compositor, xdg_wm_base, etc.
        // TODO: wl_display_roundtrip(g_wlDisplay) to receive initial registry events
    }

    void shutdown()
    {
        // TODO: wl_display_disconnect(g_wlDisplay)
    }

    void runMessageLoop()
    {
        // TODO: while (wl_display_dispatch(g_wlDisplay) != -1) {}
    }

    bool processMessages()
    {
        // TODO: wl_display_dispatch_pending(g_wlDisplay); wl_display_flush(g_wlDisplay)
        return true;
    }

    bool isKeyPressed(KeyCode) { return false; }
    bool isKeyToggled(KeyCode) { return false; }

    Point getMousePosition()
    {
        // TODO: last pointer position from wl_pointer.motion event
        return { 0, 0 };
    }

    void moveMouse(Point)
    {
        // Not supported on Wayland — pointer position is compositor-controlled.
    }

    void browseToFile(const std::filesystem::path&)
    {
        // TODO: xdg-desktop-portal org.freedesktop.portal.OpenURI.OpenFile
    }
}
#else
// ---------------------------------------------------------------------------
// Null stubs for any other platform (X11 scaffold, headless builds, etc.)
// ---------------------------------------------------------------------------
namespace LWS::Platform
{
    void init() {}
    void shutdown() {}
    void runMessageLoop() {}
    bool processMessages() { return false; }
    bool isKeyPressed(KeyCode) { return false; }
    bool isKeyToggled(KeyCode) { return false; }
    Point getMousePosition() { return {}; }
    void moveMouse(Point) {}
    void browseToFile(const std::filesystem::path&) {}
}
#endif
