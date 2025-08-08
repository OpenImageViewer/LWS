#ifdef LWS_PLATFORM_WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <WindowsX.h>

#include <LLUtils/Exception.h>
#include <LWS/StringDefs.hpp>

#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <LWS/Win32/CursorBackendWin32.hpp>
#include <LWS/Win32/EventWin32.hpp>
#include <LWS/Win32/WindowBackendWin32.hpp>
#include "internal/DragAndDropTarget.hpp"
#include "internal/MonitorInfo.hpp"
#include "internal/WindowPosHelper.hpp"

namespace
{
    static constexpr LWS::char_type g_windowClassName[] = L"LWS_WINDOW_CLASS";
    static constexpr LWS::char_type g_windowPropName[] = L"LWSBackend";

    static LWS::KeyCode keyCodeFromVirtualKey(WPARAM virtual_key, LPARAM lparam)
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
        default: return LWS::KeyCode::Unknown;
        }
    }
}

namespace LWS
{
    WindowBackendWin32::WindowBackendWin32() = default;

    WindowBackendWin32::~WindowBackendWin32()
    {
        destroy();
        if (fBackgroundBrush != nullptr)
        {
            DeleteObject(fBackgroundBrush);
            fBackgroundBrush = nullptr;
        }
    }

    Result WindowBackendWin32::create(const WindowConfig& config)
    {
        if (fHwnd != nullptr)
        {
            return Result::AlreadyCreated;
        }

        HINSTANCE instance = GetModuleHandle(nullptr);
        static std::once_flag s_windowClassOnce;
        std::call_once(s_windowClassOnce, [instance]()
        {
            WNDCLASSEX window_class{};
            window_class.cbSize = sizeof(window_class);
            window_class.lpfnWndProc = WndProc;
            window_class.hInstance = instance;
            window_class.hCursor = LoadCursor(nullptr, IDC_ARROW);
            window_class.hbrBackground = nullptr;
            window_class.lpszClassName = g_windowClassName;
            if (RegisterClassEx(&window_class) == 0)
            {
                throw std::runtime_error("Failed to register Win32 window class.");
            }
        });

        fEraseBackground = config.eraseBackground;
        fAlwaysOnTop = config.alwaysOnTop;
        fTransparent = config.transparent;
        fMinSize = config.minSize;
        fMaxSize = config.maxSize;
        fWindowStyles = config.styles;
        fDisplayState = config.displayState;
        fVisible = config.visible;
        fLastMousePos = {};
        updateBackgroundBrush();

        DWORD ex_style = 0;
        if (fAlwaysOnTop)
        {
            ex_style |= WS_EX_TOPMOST;
        }
        if (fTransparent)
        {
            ex_style |= WS_EX_LAYERED;
        }

        HWND parent_hwnd = nullptr;
        if (fParentBackend != nullptr)
        {
            parent_hwnd = reinterpret_cast<HWND>(fParentBackend->getHandle());
        }

        HWND hwnd = CreateWindowEx(
            ex_style,
            g_windowClassName,
            config.title.c_str(),
            static_cast<DWORD>(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | composeWindowStyles()),
            config.position.x,
            config.position.y,
            config.size.x,
            config.size.y,
            parent_hwnd,
            nullptr,
            instance,
            this);

        if (hwnd == nullptr || fHwnd == nullptr)
        {
            if (hwnd != nullptr)
            {
                DestroyWindow(hwnd);
            }
            return Result::Failure;
        }

        if (fAlwaysOnTop)
        {
            internal::WindowPosHelper::setAlwaysOnTop(fHwnd, true);
        }

        if (fDndEnabled)
        {
            enableDragAndDrop(true);
        }

        switch (config.displayState)
        {
        case WindowDisplayState::Minimized:
            ShowWindow(fHwnd, SW_MINIMIZE);
            break;
        case WindowDisplayState::Maximized:
            ShowWindow(fHwnd, SW_MAXIMIZE);
            break;
        case WindowDisplayState::Restored:
        default:
            break;
        }

        if (config.visible)
        {
            ShowWindow(fHwnd, SW_SHOW);
        }
        else
        {
            ShowWindow(fHwnd, SW_HIDE);
        }

        UpdateWindow(fHwnd);
        return Result::Success;
    }

    void WindowBackendWin32::destroy()
    {
        if (fHwnd != nullptr)
        {
            DestroyWindow(fHwnd);
        }
    }

    void WindowBackendWin32::show()
    {
        fVisible = true;
        if (fHwnd != nullptr)
        {
            ShowWindow(fHwnd, SW_SHOW);
        }
    }

    void WindowBackendWin32::hide()
    {
        fVisible = false;
        if (fHwnd != nullptr)
        {
            ShowWindow(fHwnd, SW_HIDE);
        }
    }

    bool WindowBackendWin32::getVisible() const
    {
        return fHwnd != nullptr ? IsWindowVisible(fHwnd) != FALSE : fVisible;
    }

    void WindowBackendWin32::setDisplayState(WindowDisplayState state)
    {
        if (fHwnd == nullptr)
        {
            fDisplayState = state;
            return;
        }

        switch (state)
        {
        case WindowDisplayState::Restored:
            ShowWindow(fHwnd, SW_RESTORE);
            break;
        case WindowDisplayState::Minimized:
            ShowWindow(fHwnd, SW_MINIMIZE);
            break;
        case WindowDisplayState::Maximized:
            ShowWindow(fHwnd, SW_MAXIMIZE);
            break;
        default:
            LL_EXCEPTION_UNEXPECTED_VALUE;
        }
    }

    WindowDisplayState WindowBackendWin32::getDisplayState() const
    {
        return fDisplayState;
    }

    void WindowBackendWin32::setTitle(const LWS::string_type& title)
    {
        if (fHwnd != nullptr)
        {
            SetWindowText(fHwnd, title.c_str());
        }
    }

    LWS::string_type WindowBackendWin32::getTitle() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        int title_length = GetWindowTextLength(fHwnd);
        if (title_length <= 0)
        {
            return {};
        }

        LWS::string_type title(static_cast<size_t>(title_length) + 1U, LWS::char_type{});
        GetWindowText(fHwnd, title.data(), title_length + 1);
        title.resize(static_cast<size_t>(title_length));
        return title;
    }

    void WindowBackendWin32::setWindowIcon(const std::filesystem::path& iconPath)
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        HICON icon = static_cast<HICON>(LoadImage(nullptr, iconPath.c_str(), IMAGE_ICON, 0, 0, LR_LOADFROMFILE | LR_DEFAULTSIZE));
        if (icon != nullptr)
        {
            SendMessage(fHwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon));
            SendMessage(fHwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon));
        }
    }

    void WindowBackendWin32::setPosition(Point pos)
    {
        if (fHwnd != nullptr)
        {
            internal::WindowPosHelper::setPosition(fHwnd, pos.x, pos.y);
        }
    }

    Point WindowBackendWin32::getPosition() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        WINDOWPLACEMENT placement{};
        placement.length = sizeof(placement);
        GetWindowPlacement(fHwnd, &placement);
        return { placement.rcNormalPosition.left, placement.rcNormalPosition.top };
    }

    void WindowBackendWin32::setSize(Size sz)
    {
        if (fHwnd != nullptr)
        {
            internal::WindowPosHelper::setSize(fHwnd, sz.x, sz.y);
        }
    }

    Size WindowBackendWin32::getClientSize() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        RECT rect{};
        GetClientRect(fHwnd, &rect);
        return { rect.right - rect.left, rect.bottom - rect.top };
    }

    Rect WindowBackendWin32::getClientRect() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        RECT rect{};
        GetClientRect(fHwnd, &rect);
        return { { rect.left, rect.top }, { rect.right, rect.bottom } };
    }

    Size WindowBackendWin32::getWindowSize() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        RECT rect{};
        GetWindowRect(fHwnd, &rect);
        return { rect.right - rect.left, rect.bottom - rect.top };
    }

    void WindowBackendWin32::setPlacement(const WindowPlacement& placement)
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        internal::WindowPosHelper::setPlacement(fHwnd, placement.position.x, placement.position.y, placement.size.x, placement.size.y);
        setDisplayState(placement.displayState);
    }

    WindowPlacement WindowBackendWin32::getPlacement() const
    {
        return { getPosition(), getWindowSize(), fDisplayState };
    }

    void WindowBackendWin32::setMinMaxSize(Size minSize, Size maxSize)
    {
        fMinSize = minSize;
        fMaxSize = maxSize;
    }

    Size WindowBackendWin32::getMinSize() const
    {
        return fMinSize;
    }

    Size WindowBackendWin32::getMaxSize() const
    {
        return fMaxSize;
    }

    void WindowBackendWin32::setWindowStyles(WindowStyle styles, bool enable)
    {
        auto current = std::to_underlying(fWindowStyles);
        auto requested = std::to_underlying(styles);
        fWindowStyles = static_cast<WindowStyle>(enable ? (current | requested) : (current & ~requested));
        updateWindowStyles();
    }

    WindowStyle WindowBackendWin32::getWindowStyles() const
    {
        return fWindowStyles;
    }

    void WindowBackendWin32::setForeground()
    {
        if (fHwnd != nullptr)
        {
            SetForegroundWindow(fHwnd);
        }
    }

    void WindowBackendWin32::setFocus()
    {
        if (fHwnd != nullptr)
        {
            ::SetFocus(fHwnd);
        }
    }

    bool WindowBackendWin32::isInFocus() const
    {
        return fHwnd != nullptr && GetFocus() == fHwnd;
    }

    void WindowBackendWin32::setAlwaysOnTop(bool onTop)
    {
        fAlwaysOnTop = onTop;
        if (fHwnd != nullptr)
        {
            internal::WindowPosHelper::setAlwaysOnTop(fHwnd, onTop);
        }
    }

    bool WindowBackendWin32::getAlwaysOnTop() const
    {
        if (fHwnd == nullptr)
        {
            return fAlwaysOnTop;
        }

        return (GetWindowLong(fHwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    }

    void WindowBackendWin32::setTransparent(bool transparent)
    {
        fTransparent = transparent;
        if (fHwnd != nullptr)
        {
            LONG ex_style = GetWindowLong(fHwnd, GWL_EXSTYLE);
            if (transparent)
            {
                ex_style |= WS_EX_LAYERED;
            }
            else
            {
                ex_style &= ~WS_EX_LAYERED;
            }

            SetWindowLong(fHwnd, GWL_EXSTYLE, ex_style);
            internal::WindowPosHelper::updateFrame(fHwnd);
        }
    }

    bool WindowBackendWin32::getTransparent() const
    {
        return fTransparent;
    }

    void WindowBackendWin32::setBackgroundColor(LLUtils::Color color)
    {
        if (fBackgroundColor != color)
        {
            fBackgroundColor = color;
            updateBackgroundBrush();
        }
    }

    LLUtils::Color WindowBackendWin32::getBackgroundColor() const
    {
        return fBackgroundColor;
    }

    void WindowBackendWin32::setEraseBackground(bool erase)
    {
        fEraseBackground = erase;
    }

    bool WindowBackendWin32::getEraseBackground() const
    {
        return fEraseBackground;
    }

    void WindowBackendWin32::setFullScreenState(FullScreenState state)
    {
        switch (state)
        {
        case FullScreenState::Windowed:
            setWindowed();
            break;
        case FullScreenState::SingleScreen:
            setFullScreen(false);
            break;
        case FullScreenState::MultiScreen:
            setFullScreen(true);
            break;
        case FullScreenState::None:
        default:
            break;
        }
    }

    FullScreenState WindowBackendWin32::getFullScreenState() const
    {
        return fFullScreenState;
    }

    bool WindowBackendWin32::isFullScreen() const
    {
        return fFullScreenState != FullScreenState::Windowed;
    }

    void WindowBackendWin32::toggleFullScreen(bool multiMonitor)
    {
        switch (fFullScreenState)
        {
        case FullScreenState::Windowed:
            setFullScreen(multiMonitor);
            break;
        case FullScreenState::SingleScreen:
            multiMonitor ? setFullScreen(true) : setWindowed();
            break;
        case FullScreenState::MultiScreen:
            setWindowed();
            break;
        case FullScreenState::None:
        default:
            break;
        }
    }

    bool WindowBackendWin32::isMouseInClientRect() const
    {
        if (fHwnd == nullptr)
        {
            return false;
        }

        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(fHwnd, &point);
        RECT rect{};
        GetClientRect(fHwnd, &rect);
        return PtInRect(&rect, point) != FALSE;
    }

    bool WindowBackendWin32::isUnderMouseCursor() const
    {
        if (fHwnd == nullptr)
        {
            return false;
        }

        POINT point{};
        GetCursorPos(&point);
        return WindowFromPoint(point) == fHwnd;
    }

    Point WindowBackendWin32::getMousePosition() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(fHwnd, &point);
        return { point.x, point.y };
    }

    void WindowBackendWin32::setLockMouseToWindowMode(LockMouseToWindowMode mode)
    {
        fLockMode = mode;
    }

    LockMouseToWindowMode WindowBackendWin32::getLockMouseToWindowMode() const
    {
        return fLockMode;
    }

    void WindowBackendWin32::setDoubleClickMode(DoubleClickMode mode)
    {
        fDoubleClickMode = mode;
    }

    DoubleClickMode WindowBackendWin32::getDoubleClickMode() const
    {
        return fDoubleClickMode;
    }

    void WindowBackendWin32::setCursor(std::shared_ptr<ICursorBackend> cursor)
    {
        fCursor = std::move(cursor);
        if (fCursor != nullptr && fCursor->backend() == BackendId::Win32)
        {
            SetCursor(static_cast<CursorBackendWin32*>(fCursor.get())->getCursorHandle());
        }
    }

    void WindowBackendWin32::setParent(IWindowBackend* parent)
    {
        fParentBackend = parent;
        if (fHwnd != nullptr)
        {
            HWND parent_hwnd = parent != nullptr ? reinterpret_cast<HWND>(parent->getHandle()) : nullptr;
            ::SetParent(fHwnd, parent_hwnd);
            setWindowStyles(WindowStyle::ChildWindow, parent_hwnd != nullptr);
        }
    }

    void WindowBackendWin32::enableDragAndDrop(bool enable)
    {
        fDndEnabled = enable;
        if (fHwnd == nullptr)
        {
            return;
        }

        bool enabled = fDragAndDrop != nullptr;
        if (enabled == enable)
        {
            return;
        }

        if (!enable)
        {
            fDragAndDrop->detach();
            fDragAndDrop.reset();
            return;
        }

        fDragAndDrop = std::make_shared<internal::DragAndDropTarget>(fHwnd, [this](const std::filesystem::path& path)
        {
            dispatchEvent(EventDragDropFile{ path });
        });
    }

    EventListenerToken WindowBackendWin32::addListener(EventCallback cb)
    {
        EventListenerToken token = fNextToken++;
        fListeners[token] = std::move(cb);
        return token;
    }

    void WindowBackendWin32::removeListener(EventListenerToken token)
    {
        fListeners.erase(token);
    }

    void WindowBackendWin32::injectRawEvent(void* platformEvent)
    {
        if (platformEvent != nullptr)
        {
            MSG* message = reinterpret_cast<MSG*>(platformEvent);
            windowProc(message->hwnd, message->message, message->wParam, message->lParam);
        }
    }

    Handle WindowBackendWin32::getHandle() const
    {
        return reinterpret_cast<Handle>(fHwnd);
    }

    BackendId WindowBackendWin32::backend() const
    {
        return BackendId::Win32;
    }

    void WindowBackendWin32::setMenuChar(bool suppress)
    {
        fSuppressMenuChar = suppress;
    }

    bool WindowBackendWin32::getMenuChar() const
    {
        return fSuppressMenuChar;
    }

    LRESULT CALLBACK WindowBackendWin32::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CREATE)
        {
            CREATESTRUCT* create_struct = reinterpret_cast<CREATESTRUCT*>(lParam);
            WindowBackendWin32* self = reinterpret_cast<WindowBackendWin32*>(create_struct->lpCreateParams);
            SetProp(hWnd, g_windowPropName, self);
            self->fHwnd = hWnd;
        }

        WindowBackendWin32* self = reinterpret_cast<WindowBackendWin32*>(GetProp(hWnd, g_windowPropName));
        if (self != nullptr)
        {
            return self->windowProc(hWnd, message, wParam, lParam);
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    LRESULT WindowBackendWin32::windowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        bool use_default = true;
        LRESULT return_value = 0;

        switch (message)
        {
        case WM_NCHITTEST:
            if (fTransparent)
            {
                use_default = false;
                return_value = HTTRANSPARENT;
                break;
            }
            if (DefWindowProc(hWnd, message, wParam, lParam) == HTCLIENT)
            {
                if (fLockMode == LockMouseToWindowMode::LockResize)
                {
                    use_default = false;
                    return_value = getCorner(MAKEPOINTS(lParam));
                }
                else if (fLockMode == LockMouseToWindowMode::LockMove)
                {
                    use_default = false;
                    return_value = HTCAPTION;
                }
            }
            break;

        case WM_SETCURSOR:
            if (fCursor != nullptr && fCursor->backend() == BackendId::Win32)
            {
                SetCursor(static_cast<CursorBackendWin32*>(fCursor.get())->getCursorHandle());
                use_default = false;
                return_value = 1;
            }
            break;

        case WM_MENUCHAR:
            if (fSuppressMenuChar)
            {
                use_default = false;
                return_value = MAKELONG(0, MNC_CLOSE);
            }
            break;

        case WM_SYSKEYDOWN:
            if (fSuppressMenuChar && wParam == VK_MENU)
            {
                use_default = false;
                return_value = 0;
                break;
            }
            [[fallthrough]];

        case WM_KEYDOWN:
        {
            bool repeat = (lParam & (1LL << 30)) != 0;
            dispatchEvent(EventKeyDown{ keyCodeFromVirtualKey(wParam, lParam), repeat });
            break;
        }

        case WM_SYSKEYUP:
        case WM_KEYUP:
            dispatchEvent(EventKeyUp{ keyCodeFromVirtualKey(wParam, lParam) });
            break;

        case WM_ERASEBKGND:
            use_default = false;
            return_value = 1;
            if (fEraseBackground && fBackgroundBrush != nullptr)
            {
                RECT rect{};
                GetClientRect(hWnd, &rect);
                FillRect(reinterpret_cast<HDC>(wParam), &rect, fBackgroundBrush);
            }
            break;

        case WM_CLOSE:
            dispatchEvent(EventClose{});
            break;

        case WM_DESTROY:
            if (fDragAndDrop != nullptr)
            {
                fDragAndDrop->detach();
                fDragAndDrop.reset();
            }
            RemoveProp(hWnd, g_windowPropName);
            fHwnd = nullptr;
            dispatchEvent(EventWindowDestroyed{});
            if (fParentBackend == nullptr)
            {
                PostQuitMessage(0);
            }
            break;

        case WM_SIZE:
        {
            WindowDisplayState new_state = fDisplayState;
            switch (wParam)
            {
            case SIZE_RESTORED:
                new_state = WindowDisplayState::Restored;
                break;
            case SIZE_MINIMIZED:
                new_state = WindowDisplayState::Minimized;
                break;
            case SIZE_MAXIMIZED:
                new_state = WindowDisplayState::Maximized;
                break;
            default:
                break;
            }

            if (new_state != fDisplayState)
            {
                fDisplayState = new_state;
                dispatchEvent(EventDisplayStateChanged{ fDisplayState });
            }

            dispatchEvent(EventResize{ { static_cast<int32_t>(LOWORD(lParam)), static_cast<int32_t>(HIWORD(lParam)) } });
            break;
        }

        case WM_MOVE:
            dispatchEvent(EventMove{ { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) } });
            break;

        case WM_SETFOCUS:
            dispatchEvent(EventFocusGained{});
            break;

        case WM_KILLFOCUS:
            dispatchEvent(EventFocusLost{});
            break;

        case WM_MOUSEMOVE:
        {
            Point position{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            Point delta{ position.x - fLastMousePos.x, position.y - fLastMousePos.y };
            fLastMousePos = position;
            dispatchEvent(EventMouseMove{ position, delta });
            break;
        }

        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        {
            bool pressed = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
            MouseButton button = MouseButton::Left;
            if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
            {
                button = MouseButton::Right;
            }
            else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP)
            {
                button = MouseButton::Middle;
            }
            else if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP)
            {
                button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
            }

            dispatchEvent(EventMouseButton{ button, pressed, { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) } });
            break;
        }

        case WM_MOUSEWHEEL:
        {
            POINT point{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            ScreenToClient(hWnd, &point);
            dispatchEvent(EventMouseWheel{ GET_WHEEL_DELTA_WPARAM(wParam), { point.x, point.y } });
            break;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT paint_struct{};
            BeginPaint(hWnd, &paint_struct);
            EndPaint(hWnd, &paint_struct);
            use_default = false;
            dispatchEvent(EventPaint{});
            break;
        }

        case WM_SHOWWINDOW:
            fVisible = wParam != 0;
            break;

        case WM_NCLBUTTONDBLCLK:
            if (fDoubleClickMode == DoubleClickMode::Default)
            {
                if (static_cast<WPARAM>(DefWindowProc(hWnd, WM_NCHITTEST, wParam, lParam)) != wParam)
                {
                    use_default = false;
                    return_value = 0;
                }
            }
            break;

        case WM_GETMINMAXINFO:
            if (fMinSize.x > 0 || fMinSize.y > 0 || fMaxSize.x > 0 || fMaxSize.y > 0)
            {
                MINMAXINFO* min_max_info = reinterpret_cast<MINMAXINFO*>(lParam);
                if (fMinSize.x > 0) min_max_info->ptMinTrackSize.x = fMinSize.x;
                if (fMinSize.y > 0) min_max_info->ptMinTrackSize.y = fMinSize.y;
                if (fMaxSize.x > 0) min_max_info->ptMaxTrackSize.x = fMaxSize.x;
                if (fMaxSize.y > 0) min_max_info->ptMaxTrackSize.y = fMaxSize.y;
                use_default = false;
                return_value = 0;
            }
            break;

        default:
            break;
        }

        Win32::WinMessage raw_message{
            reinterpret_cast<uintptr_t>(hWnd),
            message,
            static_cast<uintptr_t>(wParam),
            static_cast<uintptr_t>(lParam)
        };
        dispatchEvent(EventRawPlatform{ std::to_underlying(BackendId::Win32), &raw_message });

        return use_default ? DefWindowProc(hWnd, message, wParam, lParam) : return_value;
    }

    LONG WindowBackendWin32::composeWindowStyles() const
    {
        if (fFullScreenState != FullScreenState::Windowed)
        {
            return 0;
        }

        LONG styles = 0;
        auto window_styles = std::to_underlying(fWindowStyles);
        auto has_style = [window_styles](WindowStyle style)
        {
            return (window_styles & std::to_underlying(style)) != 0;
        };

        if (has_style(WindowStyle::ChildWindow)) styles |= WS_CHILD;
        if (has_style(WindowStyle::Caption)) styles |= WS_CAPTION;
        if (has_style(WindowStyle::CloseButton)) styles |= WS_SYSMENU | WS_CAPTION;
        if (has_style(WindowStyle::MinimizeButton)) styles |= WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX;
        if (has_style(WindowStyle::MaximizeButton)) styles |= WS_SYSMENU | WS_CAPTION | WS_MAXIMIZEBOX;
        if (has_style(WindowStyle::ResizableBorder)) styles |= WS_SIZEBOX;

        return styles;
    }

    void WindowBackendWin32::updateWindowStyles()
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        LONG style = static_cast<LONG>(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | (fVisible ? WS_VISIBLE : 0));
        SetWindowLong(fHwnd, GWL_STYLE, style | composeWindowStyles());
        internal::WindowPosHelper::updateFrame(fHwnd);
    }

    void WindowBackendWin32::updateBackgroundBrush()
    {
        if (fBackgroundBrush != nullptr)
        {
            DeleteObject(fBackgroundBrush);
            fBackgroundBrush = nullptr;
        }

        fBackgroundBrush = CreateSolidBrush(RGB(fBackgroundColor.R(), fBackgroundColor.G(), fBackgroundColor.B()));
    }

    void WindowBackendWin32::setWindowed()
    {
        if (fHwnd == nullptr)
        {
            fFullScreenState = FullScreenState::Windowed;
            return;
        }

        fFullScreenState = FullScreenState::Windowed;
        updateWindowStyles();
        if (fSavedFullScreenPlacement.length == sizeof(WINDOWPLACEMENT))
        {
            SetWindowPlacement(fHwnd, &fSavedFullScreenPlacement);
        }
    }

    void WindowBackendWin32::setFullScreen(bool multiMonitor)
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        if (fFullScreenState == FullScreenState::Windowed)
        {
            fSavedFullScreenPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(fHwnd, &fSavedFullScreenPlacement);
        }

        internal::MonitorInfo& monitor_info = internal::MonitorInfo::instance();
        monitor_info.refresh();

        RECT rect{};
        if (multiMonitor)
        {
            rect = monitor_info.getBoundingMonitorArea();
            fFullScreenState = FullScreenState::MultiScreen;
        }
        else
        {
            rect = monitor_info.getMonitorInfo(MonitorFromWindow(fHwnd, MONITOR_DEFAULTTOPRIMARY)).monitorInfo.rcMonitor;
            fFullScreenState = FullScreenState::SingleScreen;
        }

        updateWindowStyles();
        SetWindowPos(
            fHwnd,
            nullptr,
            rect.left,
            rect.top,
            rect.right - rect.left,
            rect.bottom - rect.top,
            internal::WindowPosHelper::composeFlags({ internal::WindowPosOp::Placement, internal::WindowPosOp::UpdateFrame }));
    }

    LRESULT WindowBackendWin32::getCorner(POINTS points) const
    {
        POINT point{ points.x, points.y };
        ScreenToClient(fHwnd, &point);

        Size window_size = getWindowSize();
        auto distance_squared = [](int32_t ax, int32_t ay, int32_t bx, int32_t by) -> int64_t
        {
            int64_t dx = static_cast<int64_t>(ax) - bx;
            int64_t dy = static_cast<int64_t>(ay) - by;
            return dx * dx + dy * dy;
        };

        int64_t distances[4] = {
            distance_squared(point.x, point.y, 0, 0),
            distance_squared(point.x, point.y, window_size.x, 0),
            distance_squared(point.x, point.y, window_size.x, window_size.y),
            distance_squared(point.x, point.y, 0, window_size.y)
        };

        int closest_index = 0;
        for (int index = 1; index < 4; ++index)
        {
            if (distances[index] < distances[closest_index])
            {
                closest_index = index;
            }
        }

        static constexpr LRESULT corners[4] = { HTTOPLEFT, HTTOPRIGHT, HTBOTTOMRIGHT, HTBOTTOMLEFT };
        return corners[closest_index];
    }

    void WindowBackendWin32::dispatchEvent(const AnyEvent& event_data)
    {
        std::vector<EventListenerToken> tokens;
        tokens.reserve(fListeners.size());
        for (const auto& [token, _] : fListeners)
        {
            tokens.push_back(token);
        }

        for (EventListenerToken token : tokens)
        {
            auto it = fListeners.find(token);
            if (it != fListeners.end() && it->second(event_data))
            {
                break;
            }
        }
    }
}
#endif // LWS_PLATFORM_WIN32