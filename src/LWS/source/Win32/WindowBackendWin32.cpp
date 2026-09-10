#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <WindowsX.h>

    #include <LLUtils/Exception.h>
    #include <LWS/StringDefs.hpp>

    #include <mutex>
    #include <cmath>
    #include <optional>
    #include <stdexcept>
    #include <utility>
    #include <tuple>

    #include <LWS/source/Win32/internal/CursorBackendWin32.hpp>
    #include <LWS/Win32/EventWin32.hpp>
    #include <LWS/source/Win32/internal/WindowBackendWin32.hpp>
    #include <LWS/Platform.hpp>
    #include "internal/DragAndDropTarget.hpp"
    #include "internal/MonitorInfo.hpp"
    #include "internal/PlatformState.hpp"
    #include "internal/WindowPosHelper.hpp"
    #include "../internal/WindowBackendAccess.hpp"
    #include "../internal/BitmapValidation.hpp"

namespace
{
    static constexpr LWS::char_type g_windowClassName[] = L"LWS_WINDOW_CLASS";
    static constexpr LWS::char_type g_windowPropName[] = L"LWSBackend";
    static constexpr int g_logicalDpi = 96;

    // Resolve the Windows 10 DPI helpers dynamically so the same binary remains loadable on Windows 7 and 8.
    UINT systemDpi()
    {
        using GetDpiForSystemFn = UINT(WINAPI*)();
        static const auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
        if (getDpiForSystem != nullptr)
            return getDpiForSystem();

        static const UINT legacyDpi = []
        {
            const HDC deviceContext = GetDC(nullptr);
            const UINT dpi = deviceContext != nullptr ? static_cast<UINT>(GetDeviceCaps(deviceContext, LOGPIXELSX))
                                                      : static_cast<UINT>(g_logicalDpi);
            if (deviceContext != nullptr)
                ReleaseDC(nullptr, deviceContext);
            return dpi;
        }();
        return legacyDpi;
    }

    UINT windowDpi(HWND window)
    {
        using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
        static const auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
        if (getDpiForWindow != nullptr)
            return getDpiForWindow(window);
        return systemDpi();
    }

    void adjustWindowRectForDpi(RECT& rectangle, DWORD style, DWORD extendedStyle, UINT dpi)
    {
        using AdjustWindowRectExForDpiFn = BOOL(WINAPI*)(LPRECT, DWORD, BOOL, DWORD, UINT);
        static const auto adjustWindowRectExForDpi = reinterpret_cast<AdjustWindowRectExForDpiFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "AdjustWindowRectExForDpi"));
        if (adjustWindowRectExForDpi != nullptr)
            std::ignore = adjustWindowRectExForDpi(&rectangle, style, FALSE, extendedStyle, dpi);
        else
            std::ignore = AdjustWindowRectEx(&rectangle, style, FALSE, extendedStyle);
    }

    int systemMetricForDpi(int index, UINT dpi)
    {
        using GetSystemMetricsForDpiFn = int(WINAPI*)(int, UINT);
        static const auto getSystemMetricsForDpi = reinterpret_cast<GetSystemMetricsForDpiFn>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi"));
        return getSystemMetricsForDpi != nullptr ? getSystemMetricsForDpi(index, dpi) : GetSystemMetrics(index);
    }

    int32_t physicalFromLogical(int32_t value, UINT dpi)
    {
        return MulDiv(value, static_cast<int>(dpi), g_logicalDpi);
    }

    int32_t logicalFromPhysical(int32_t value, UINT dpi)
    {
        return MulDiv(value, g_logicalDpi, static_cast<int>(dpi));
    }

    LWS::Point physicalFromLogical(LWS::Point point, UINT dpi)
    {
        return {physicalFromLogical(point.x, dpi), physicalFromLogical(point.y, dpi)};
    }

    LWS::Point logicalFromPhysical(LWS::Point point, UINT dpi)
    {
        return {logicalFromPhysical(point.x, dpi), logicalFromPhysical(point.y, dpi)};
    }

    LWS::Size outerSizeForLogicalClient(LWS::Size clientSize, DWORD style, DWORD extendedStyle, UINT dpi)
    {
        RECT rectangle{0, 0, physicalFromLogical(clientSize.x, dpi), physicalFromLogical(clientSize.y, dpi)};
        adjustWindowRectForDpi(rectangle, style, extendedStyle, dpi);
        if ((style & WS_VSCROLL) != 0)
            rectangle.right += systemMetricForDpi(SM_CXVSCROLL, dpi);
        if ((style & WS_HSCROLL) != 0)
            rectangle.bottom += systemMetricForDpi(SM_CYHSCROLL, dpi);
        return {rectangle.right - rectangle.left, rectangle.bottom - rectangle.top};
    }

    static LWS::KeyCode keyCodeFromVirtualKey(WPARAM virtual_key, LPARAM lparam)
    {
        switch (virtual_key)
        {
            case 'A':
                return LWS::KeyCode::A;
            case 'B':
                return LWS::KeyCode::B;
            case 'C':
                return LWS::KeyCode::C;
            case 'D':
                return LWS::KeyCode::D;
            case 'E':
                return LWS::KeyCode::E;
            case 'F':
                return LWS::KeyCode::F;
            case 'G':
                return LWS::KeyCode::G;
            case 'H':
                return LWS::KeyCode::H;
            case 'I':
                return LWS::KeyCode::I;
            case 'J':
                return LWS::KeyCode::J;
            case 'K':
                return LWS::KeyCode::K;
            case 'L':
                return LWS::KeyCode::L;
            case 'M':
                return LWS::KeyCode::M;
            case 'N':
                return LWS::KeyCode::N;
            case 'O':
                return LWS::KeyCode::O;
            case 'P':
                return LWS::KeyCode::P;
            case 'Q':
                return LWS::KeyCode::Q;
            case 'R':
                return LWS::KeyCode::R;
            case 'S':
                return LWS::KeyCode::S;
            case 'T':
                return LWS::KeyCode::T;
            case 'U':
                return LWS::KeyCode::U;
            case 'V':
                return LWS::KeyCode::V;
            case 'W':
                return LWS::KeyCode::W;
            case 'X':
                return LWS::KeyCode::X;
            case 'Y':
                return LWS::KeyCode::Y;
            case 'Z':
                return LWS::KeyCode::Z;
            case '0':
                return LWS::KeyCode::Digit0;
            case '1':
                return LWS::KeyCode::Digit1;
            case '2':
                return LWS::KeyCode::Digit2;
            case '3':
                return LWS::KeyCode::Digit3;
            case '4':
                return LWS::KeyCode::Digit4;
            case '5':
                return LWS::KeyCode::Digit5;
            case '6':
                return LWS::KeyCode::Digit6;
            case '7':
                return LWS::KeyCode::Digit7;
            case '8':
                return LWS::KeyCode::Digit8;
            case '9':
                return LWS::KeyCode::Digit9;
            case VK_F1:
                return LWS::KeyCode::F1;
            case VK_F2:
                return LWS::KeyCode::F2;
            case VK_F3:
                return LWS::KeyCode::F3;
            case VK_F4:
                return LWS::KeyCode::F4;
            case VK_F5:
                return LWS::KeyCode::F5;
            case VK_F6:
                return LWS::KeyCode::F6;
            case VK_F7:
                return LWS::KeyCode::F7;
            case VK_F8:
                return LWS::KeyCode::F8;
            case VK_F9:
                return LWS::KeyCode::F9;
            case VK_F10:
                return LWS::KeyCode::F10;
            case VK_F11:
                return LWS::KeyCode::F11;
            case VK_F12:
                return LWS::KeyCode::F12;
            case VK_LEFT:
                return LWS::KeyCode::Left;
            case VK_RIGHT:
                return LWS::KeyCode::Right;
            case VK_UP:
                return LWS::KeyCode::Up;
            case VK_DOWN:
                return LWS::KeyCode::Down;
            case VK_HOME:
                return LWS::KeyCode::Home;
            case VK_END:
                return LWS::KeyCode::End;
            case VK_PRIOR:
                return LWS::KeyCode::PageUp;
            case VK_NEXT:
                return LWS::KeyCode::PageDown;
            case VK_INSERT:
                return LWS::KeyCode::Insert;
            case VK_DELETE:
                return LWS::KeyCode::Delete;
            case VK_RETURN:
                return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::NumpadEnter : LWS::KeyCode::Enter;
            case VK_ESCAPE:
                return LWS::KeyCode::Escape;
            case VK_TAB:
                return LWS::KeyCode::Tab;
            case VK_BACK:
                return LWS::KeyCode::Backspace;
            case VK_SPACE:
                return LWS::KeyCode::Space;
            case VK_SHIFT:
            {
                UINT scancode = static_cast<UINT>((lparam >> 16) & 0xFF);
                UINT mapped = MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX);
                return mapped == VK_RSHIFT ? LWS::KeyCode::RShift : LWS::KeyCode::LShift;
            }
            case VK_CONTROL:
                return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::RControl : LWS::KeyCode::LControl;
            case VK_MENU:
                return (lparam & (1LL << 24)) != 0 ? LWS::KeyCode::RAlt : LWS::KeyCode::LAlt;
            case VK_LWIN:
            case VK_RWIN:
                return LWS::KeyCode::Win;
            case VK_CAPITAL:
                return LWS::KeyCode::CapsLock;
            case VK_NUMLOCK:
                return LWS::KeyCode::NumLock;
            case VK_SCROLL:
                return LWS::KeyCode::ScrollLock;
            case VK_NUMPAD0:
                return LWS::KeyCode::Numpad0;
            case VK_NUMPAD1:
                return LWS::KeyCode::Numpad1;
            case VK_NUMPAD2:
                return LWS::KeyCode::Numpad2;
            case VK_NUMPAD3:
                return LWS::KeyCode::Numpad3;
            case VK_NUMPAD4:
                return LWS::KeyCode::Numpad4;
            case VK_NUMPAD5:
                return LWS::KeyCode::Numpad5;
            case VK_NUMPAD6:
                return LWS::KeyCode::Numpad6;
            case VK_NUMPAD7:
                return LWS::KeyCode::Numpad7;
            case VK_NUMPAD8:
                return LWS::KeyCode::Numpad8;
            case VK_NUMPAD9:
                return LWS::KeyCode::Numpad9;
            case VK_ADD:
                return LWS::KeyCode::NumpadAdd;
            case VK_SUBTRACT:
                return LWS::KeyCode::NumpadSubtract;
            case VK_MULTIPLY:
                return LWS::KeyCode::NumpadMultiply;
            case VK_DIVIDE:
                return LWS::KeyCode::NumpadDivide;
            case VK_DECIMAL:
                return LWS::KeyCode::NumpadDecimal;
            case VK_OEM_COMMA:
                return LWS::KeyCode::Comma;
            case VK_OEM_PERIOD:
                return LWS::KeyCode::Period;
            case VK_OEM_2:
                return LWS::KeyCode::Slash;
            case VK_OEM_1:
                return LWS::KeyCode::Semicolon;
            case VK_OEM_7:
                return LWS::KeyCode::Quote;
            case VK_OEM_4:
                return LWS::KeyCode::LeftBracket;
            case VK_OEM_6:
                return LWS::KeyCode::RightBracket;
            case VK_OEM_5:
                return LWS::KeyCode::Backslash;
            case VK_OEM_MINUS:
                return LWS::KeyCode::Minus;
            case VK_OEM_PLUS:
                return LWS::KeyCode::Equals;
            case VK_OEM_3:
                return LWS::KeyCode::Tilde;
            case VK_SNAPSHOT:
                return LWS::KeyCode::PrintScreen;
            case VK_PAUSE:
                return LWS::KeyCode::Pause;
            default:
                return LWS::KeyCode::Unknown;
        }
    }
}  // namespace

namespace LWS
{
    WindowBackendWin32::WindowBackendWin32(Window& owner, internal::MonitorInfo& monitors)
        : internal::IWindowBackend(owner), fMonitors(monitors)
    {
    }

    WindowBackendWin32::~WindowBackendWin32()
    {
        destroy();
        if (fBackgroundBrush != nullptr)
        {
            DeleteObject(fBackgroundBrush);
            fBackgroundBrush = nullptr;
        }
    }

    Result WindowBackendWin32::create(const internal::NativeWindowConfig& config)
    {
        if (fHwnd != nullptr)
        {
            return Result::AlreadyCreated;
        }

        HINSTANCE instance = GetModuleHandle(nullptr);
        static std::once_flag s_windowClassOnce;
        std::call_once(s_windowClassOnce,
                       [instance]()
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
        if (fTransparent && fParentBackend == nullptr)
        {
            ex_style |= WS_EX_LAYERED;
        }

        HWND parent_hwnd = nullptr;
        if (fParentBackend != nullptr)
        {
            parent_hwnd = reinterpret_cast<HWND>(fParentBackend->getHandle());
        }

        const DWORD style = static_cast<DWORD>(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | composeWindowStyles());
        const UINT dpi = parent_hwnd != nullptr ? windowDpi(parent_hwnd) : systemDpi();
        const Point position = physicalFromLogical(config.position, dpi);
        const Size outerSize = outerSizeForLogicalClient(config.size, style, ex_style, dpi);
        HWND hwnd = CreateWindowEx(ex_style, g_windowClassName, config.title.c_str(), style, position.x, position.y,
                                   outerSize.x, outerSize.y, parent_hwnd, nullptr, instance, this);

        if (hwnd == nullptr || fHwnd == nullptr)
        {
            if (hwnd != nullptr)
            {
                DestroyWindow(hwnd);
            }
            return Result::Failure;
        }

        // Creation can select another monitor DPI or add native frame styles; honor the requested client size.
        if (getClientSize() != config.size)
            setSize(config.size);
        applyWindowIcon();

        if (fTransparent && fParentBackend != nullptr)
        {
            // Layered child creation is rejected when the host manifest is not Windows-8-aware. Applying the style
            // after creation preserves hit-test transparency for every host and enables visual transparency when the
            // host supports layered children.
            setTransparent(true);
        }

        if (fAlwaysOnTop)
        {
            internal::WindowPosHelper::setAlwaysOnTop(fHwnd, true);
        }

        if (fDndEnabled)
        {
            const Result dragAndDropResult = enableDragAndDrop(true);
            if (dragAndDropResult != Result::Success)
            {
                destroy();
                return dragAndDropResult;
            }
        }

        switch (config.displayState)
        {
            case WindowShowState::Minimized:
                ShowWindow(fHwnd, SW_MINIMIZE);
                break;
            case WindowShowState::Maximized:
                ShowWindow(fHwnd, SW_MAXIMIZE);
                break;
            case WindowShowState::Restored:
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
        if (fWindowIcon != nullptr)
            DestroyIcon(std::exchange(fWindowIcon, nullptr));
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

    void WindowBackendWin32::setDisplayState(WindowShowState state)
    {
        if (state == fDisplayState && fVisible)
            return;
        if (fHwnd == nullptr)
        {
            fDisplayState = state;
            return;
        }

        switch (state)
        {
            case WindowShowState::Restored:
                ShowWindow(fHwnd, SW_RESTORE);
                break;
            case WindowShowState::Minimized:
                ShowWindow(fHwnd, SW_MINIMIZE);
                break;
            case WindowShowState::Maximized:
                ShowWindow(fHwnd, SW_MAXIMIZE);
                break;
            default:
                LL_EXCEPTION_UNEXPECTED_VALUE;
        }
    }

    void WindowBackendWin32::maximize()
    {
        if (fFullScreenState != internal::FullScreenState::Windowed)
        {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (!GetMonitorInfoW(MonitorFromWindow(fHwnd, MONITOR_DEFAULTTONEAREST), &monitor))
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Unable to obtain the maximization monitor");
            fFullScreenState = internal::FullScreenState::Windowed;
            updateWindowStyles();

            // Apply the normal placement and maximized state together, without visiting the old fullscreen monitor.
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE));
            const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
            const Size outer = outerSizeForLogicalClient(fSavedWindowedClientSize, style, extendedStyle, fDpi);
            const int width = std::min<int>(outer.x, monitor.rcWork.right - monitor.rcWork.left);
            const int height = std::min<int>(outer.y, monitor.rcWork.bottom - monitor.rcWork.top);
            RECT bounds = monitor.rcWork;
            if ((extendedStyle & WS_EX_TOOLWINDOW) == 0)
                OffsetRect(&bounds, monitor.rcMonitor.left - monitor.rcWork.left,
                           monitor.rcMonitor.top - monitor.rcWork.top);
            WINDOWPLACEMENT placement = fSavedFullScreenPlacement;
            const int left = std::clamp(placement.rcNormalPosition.left, bounds.left, bounds.right - width);
            const int top = std::clamp(placement.rcNormalPosition.top, bounds.top, bounds.bottom - height);
            placement.rcNormalPosition = {left, top, left + width, top + height};
            placement.showCmd = SW_SHOWMAXIMIZED;
            placement.flags = 0;
            SetWindowPlacement(fHwnd, &placement);
        }
        else
        {
            setDisplayState(WindowShowState::Maximized);
        }
    }

    WindowShowState WindowBackendWin32::getDisplayState() const
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

    Result WindowBackendWin32::setWindowIcon(const BitmapBuffer* icon)
    {
        if (icon == nullptr)
        {
            const HICON previousIcon = std::exchange(fWindowIcon, nullptr);
            applyWindowIcon();
            if (previousIcon != nullptr)
                DestroyIcon(previousIcon);
            return Result::Success;
        }
        const auto layout = internal::validateBitmapBuffer(*icon);
        if (!layout.has_value() || icon->format != BitmapPixelFormat::Bgra8Premultiplied)
            return Result::InvalidArgument;

        BITMAPV5HEADER header{};
        header.bV5Size = sizeof(header);
        header.bV5Width = static_cast<LONG>(icon->width);
        header.bV5Height = -static_cast<LONG>(icon->height);
        header.bV5Planes = 1;
        header.bV5BitCount = 32;
        header.bV5Compression = BI_BITFIELDS;
        header.bV5RedMask = 0x00FF0000;
        header.bV5GreenMask = 0x0000FF00;
        header.bV5BlueMask = 0x000000FF;
        header.bV5AlphaMask = 0xFF000000;
        void* targetPixels{};
        HDC screen = GetDC(nullptr);
        HBITMAP color = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &targetPixels,
                                         nullptr, 0);
        ReleaseDC(nullptr, screen);
        if (color == nullptr || targetPixels == nullptr)
            return Result::Failure;
        const size_t rowBytes = static_cast<size_t>(icon->width) * 4U;
        for (uint32_t row = 0; row < icon->height; ++row)
            std::memcpy(static_cast<std::byte*>(targetPixels) + row * rowBytes,
                        icon->pixels.data() + row * icon->rowPitch, rowBytes);
        HBITMAP mask = CreateBitmap(static_cast<int>(icon->width), static_cast<int>(icon->height), 1, 1, nullptr);
        ICONINFO info{.fIcon = TRUE, .hbmMask = mask, .hbmColor = color};
        HICON nativeIcon = mask != nullptr ? CreateIconIndirect(&info) : nullptr;
        DeleteObject(color);
        if (mask != nullptr)
            DeleteObject(mask);
        if (nativeIcon == nullptr)
            return Result::Failure;
        const HICON previousIcon = std::exchange(fWindowIcon, nativeIcon);
        applyWindowIcon();
        if (previousIcon != nullptr)
            DestroyIcon(previousIcon);
        return Result::Success;
    }

    void WindowBackendWin32::applyWindowIcon() const
    {
        if (fHwnd != nullptr)
        {
            SendMessageW(fHwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(fWindowIcon));
            SendMessageW(fHwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(fWindowIcon));
        }
    }

    void WindowBackendWin32::setPosition(Point pos)
    {
        if (fHwnd != nullptr)
        {
            const Point position = physicalFromLogical(pos, fDpi);
            internal::WindowPosHelper::setPosition(fHwnd, position.x, position.y);
        }
    }

    Point WindowBackendWin32::getPosition() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        POINT position{};
        if (fParentBackend != nullptr)
        {
            RECT rectangle{};
            GetWindowRect(fHwnd, &rectangle);
            position = {rectangle.left, rectangle.top};
            ScreenToClient(reinterpret_cast<HWND>(fParentBackend->getHandle()), &position);
        }
        else
        {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            GetWindowPlacement(fHwnd, &placement);
            position = {placement.rcNormalPosition.left, placement.rcNormalPosition.top};
        }
        return logicalFromPhysical(Point{position.x, position.y}, fDpi);
    }

    void WindowBackendWin32::setSize(Size sz)
    {
        if (fHwnd != nullptr)
        {
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE));
            const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
            const Size outerSize = outerSizeForLogicalClient(sz, style, extendedStyle, fDpi);
            internal::WindowPosHelper::setSize(fHwnd, outerSize.x, outerSize.y);
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
        return {MulDiv(rect.right - rect.left, 96, static_cast<int>(fDpi)),
                MulDiv(rect.bottom - rect.top, 96, static_cast<int>(fDpi))};
    }

    Size WindowBackendWin32::getFramebufferSize() const
    {
        if (fHwnd == nullptr)
            return {};
        RECT rect{};
        GetClientRect(fHwnd, &rect);
        return {rect.right - rect.left, rect.bottom - rect.top};
    }

    Size WindowBackendWin32::getWindowSize() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        RECT rect{};
        GetWindowRect(fHwnd, &rect);
        return {rect.right - rect.left, rect.bottom - rect.top};
    }

    void WindowBackendWin32::setPlacement(const internal::NativeWindowPlacement& placement)
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        const Point position = physicalFromLogical(placement.position, fDpi);
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
        const Size outerSize = outerSizeForLogicalClient(placement.size, style, extendedStyle, fDpi);
        internal::WindowPosHelper::setPlacement(fHwnd, position.x, position.y, outerSize.x, outerSize.y);
        if (placement.displayState != fDisplayState)
            setDisplayState(placement.displayState);
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

    void WindowBackendWin32::setEraseBackground(bool erase)
    {
        fEraseBackground = erase;
    }

    bool WindowBackendWin32::getEraseBackground() const
    {
        return fEraseBackground;
    }

    void WindowBackendWin32::setFullScreenState(internal::FullScreenState state)
    {
        if (state == fFullScreenState)
            return;
        switch (state)
        {
            case internal::FullScreenState::Windowed:
                setWindowed();
                break;
            case internal::FullScreenState::SingleScreen:
                setFullScreen(false);
                break;
            case internal::FullScreenState::MultiScreen:
                setFullScreen(true);
                break;
            case internal::FullScreenState::None:
            default:
                break;
        }
    }

    internal::FullScreenState WindowBackendWin32::getFullScreenState() const
    {
        return fFullScreenState;
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

    Point WindowBackendWin32::getMousePosition() const
    {
        if (fHwnd == nullptr)
        {
            return {};
        }

        POINT point{};
        GetCursorPos(&point);
        ScreenToClient(fHwnd, &point);
        return logicalFromPhysical(Point{point.x, point.y}, fDpi);
    }

    void WindowBackendWin32::setLockMouseToWindowMode(internal::LockMouseToWindowMode mode)
    {
        if (fHwnd != nullptr && mode != internal::LockMouseToWindowMode::NoLock)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) != FALSE)
            {
                const LPARAM position = MAKELPARAM(cursor.x, cursor.y);
                const LRESULT hitTest = mode == internal::LockMouseToWindowMode::LockMove
                                            ? HTCAPTION
                                            : getCorner(MAKEPOINTS(position));
                ReleaseCapture();
                SendMessageW(fHwnd, WM_NCLBUTTONDOWN, hitTest, position);
            }
        }
    }

    Result WindowBackendWin32::setPointerLocked(bool)
    {
        return Result::NotSupported;
    }

    void WindowBackendWin32::setCursor(std::shared_ptr<internal::ICursorBackend> cursor)
    {
        fCursor = std::move(cursor);
        if (fCursor != nullptr && fCursor->backend() == BackendId::Win32)
        {
            SetCursor(static_cast<CursorBackendWin32*>(fCursor.get())->getCursorHandle());
        }
    }

    void WindowBackendWin32::setParent(internal::IWindowBackend* parent)
    {
        fParentBackend = parent;
    }

    void WindowBackendWin32::dispatchClientAreaSizeChanged(Size framebufferSize)
    {
        const double scale = static_cast<double>(fDpi) / 96.0;
        const Size logicalSize{static_cast<int32_t>(std::lround(framebufferSize.x / scale)),
                               static_cast<int32_t>(std::lround(framebufferSize.y / scale))};
        std::ignore = dispatchEvent(
            EventClientAreaSizeChanged{{{logicalSize.x, logicalSize.y}, {framebufferSize.x, framebufferSize.y}}});
    }

    Result WindowBackendWin32::enableDragAndDrop(bool enable)
    {
        if (!enable)
        {
            fDndEnabled = false;
            if (fDragAndDrop != nullptr)
            {
                fDragAndDrop->detach();
                fDragAndDrop.reset();
            }
            return Result::Success;
        }

        if (!internal::isOleInitializedForCurrentThread())
            return Result::NotSupported;

        fDndEnabled = true;
        if (fHwnd == nullptr || fDragAndDrop != nullptr)
            return Result::Success;

        auto dragAndDrop = std::make_shared<internal::DragAndDropTarget>(fHwnd,
                                                                         [this](const std::filesystem::path& path)
                                                                         { dispatchEvent(EventDragDropFile{path}); });
        if (FAILED(dragAndDrop->getAttachResult()))
        {
            fDndEnabled = false;
            return Result::Failure;
        }

        fDragAndDrop = std::move(dragAndDrop);
        return Result::Success;
    }

    Handle WindowBackendWin32::getHandle() const
    {
        return reinterpret_cast<Handle>(fHwnd);
    }

    uintptr_t WindowBackendWin32::getCurrentMonitorHandle() const
    {
        return reinterpret_cast<uintptr_t>(MonitorFromWindow(fHwnd, MONITOR_DEFAULTTOPRIMARY));
    }

    BackendId WindowBackendWin32::backend() const
    {
        return BackendId::Win32;
    }

    void WindowBackendWin32::setMenuChar(bool suppress)
    {
        fSuppressMenuChar = suppress;
    }

    LRESULT CALLBACK WindowBackendWin32::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_CREATE)
        {
            CREATESTRUCT* create_struct = reinterpret_cast<CREATESTRUCT*>(lParam);
            WindowBackendWin32* self = reinterpret_cast<WindowBackendWin32*>(create_struct->lpCreateParams);
            SetProp(hWnd, g_windowPropName, self);
            self->fHwnd = hWnd;
            self->fDpi = windowDpi(hWnd);
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
                }
                break;

            case WM_SETCURSOR:
                if (LOWORD(lParam) == HTCLIENT && fCursor != nullptr && fCursor->backend() == BackendId::Win32)
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
                if (dispatchPlatformEvent(Win32::KeyEvent{static_cast<uint32_t>(wParam), static_cast<uint32_t>(lParam),
                                                          true},
                                          return_value))
                {
                    use_default = false;
                }
                bool repeat = (lParam & (1LL << 30)) != 0;
                dispatchEvent(EventKeyDown{keyCodeFromVirtualKey(wParam, lParam), repeat});
                break;
            }

            case WM_SYSKEYUP:
            case WM_KEYUP:
                if (dispatchPlatformEvent(Win32::KeyEvent{static_cast<uint32_t>(wParam), static_cast<uint32_t>(lParam),
                                                          false},
                                          return_value))
                {
                    use_default = false;
                }
                dispatchEvent(EventKeyUp{keyCodeFromVirtualKey(wParam, lParam)});
                break;

            case WM_VSCROLL:
            {
                std::optional<Win32::VerticalScrollAction> action;
                switch (LOWORD(wParam))
                {
                    case SB_PAGEUP:
                        action = Win32::VerticalScrollAction::PageUp;
                        break;
                    case SB_PAGEDOWN:
                        action = Win32::VerticalScrollAction::PageDown;
                        break;
                    case SB_THUMBPOSITION:
                        action = Win32::VerticalScrollAction::ThumbPosition;
                        break;
                    case SB_THUMBTRACK:
                        action = Win32::VerticalScrollAction::ThumbTrack;
                        break;
                    default:
                        break;
                }
                if (action &&
                    dispatchPlatformEvent(Win32::VerticalScrollEvent{*action, static_cast<int32_t>(HIWORD(wParam))},
                                          return_value))
                {
                    use_default = false;
                }
                break;
            }

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
                if (dispatchEvent(EventCloseRequested{}) == EventResponse::Handled)
                    use_default = false;
                break;

            case WM_DESTROY:
                if (fDragAndDrop != nullptr)
                {
                    fDragAndDrop->detach();
                    fDragAndDrop.reset();
                }
                RemoveProp(hWnd, g_windowPropName);
                fHwnd = nullptr;
                std::ignore = dispatchEvent(EventWindowDestroyed{});
                break;

            case WM_SIZE:
            {
                WindowShowState new_state = fDisplayState;
                switch (wParam)
                {
                    case SIZE_RESTORED:
                        new_state = WindowShowState::Restored;
                        break;
                    case SIZE_MINIMIZED:
                        new_state = WindowShowState::Minimized;
                        break;
                    case SIZE_MAXIMIZED:
                        new_state = WindowShowState::Maximized;
                        break;
                    default:
                        break;
                }

                if (new_state != fDisplayState)
                {
                    fDisplayState = new_state;
                    std::ignore = dispatchEvent(EventShowStateChanged{fDisplayState});
                }

                const Size framebufferSize{static_cast<int32_t>(LOWORD(lParam)), static_cast<int32_t>(HIWORD(lParam))};
                dispatchClientAreaSizeChanged(framebufferSize);
                break;
            }

            case WM_DPICHANGED:
                fDpi = LOWORD(wParam);
                if (fParentBackend == nullptr)
                {
                    if (!fRestoringFullScreenPlacement)
                    {
                        const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                        SetWindowPos(hWnd, nullptr, suggested->left, suggested->top, suggested->right - suggested->left,
                                     suggested->bottom - suggested->top,
                                     SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
                    }
                    use_default = false;
                    return_value = 0;
                }
                dispatchClientAreaSizeChanged(getFramebufferSize());
                break;

            case WM_DPICHANGED_AFTERPARENT:
                fDpi = windowDpi(hWnd);
                dispatchClientAreaSizeChanged(getFramebufferSize());
                break;

            case WM_MOVE:
                dispatchEvent(EventMove{getPosition()});
                break;

            case WM_SETFOCUS:
                dispatchEvent(EventFocusGained{});
                break;

            case WM_KILLFOCUS:
                dispatchEvent(EventFocusLost{});
                break;

            case WM_ACTIVATE:
                if (dispatchPlatformEvent(Win32::ActivationEvent{LOWORD(wParam) != WA_INACTIVE}, return_value))
                {
                    use_default = false;
                }
                break;

            case WM_MOUSEMOVE:
            {
                const Point position = logicalFromPhysical(Point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, fDpi);
                Point delta{position.x - fLastMousePos.x, position.y - fLastMousePos.y};
                fLastMousePos = position;
                dispatchEvent(EventMouseMove{position, delta});
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
                bool pressed = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN ||
                               message == WM_XBUTTONDOWN;
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

                const Point position = logicalFromPhysical(Point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, fDpi);
                dispatchEvent(EventMouseButton{button, pressed, position});
                break;
            }

            case WM_MOUSEWHEEL:
            {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hWnd, &point);
                dispatchEvent(EventMouseWheel{
                    GET_WHEEL_DELTA_WPARAM(wParam),
                    logicalFromPhysical(Point{point.x, point.y}, fDpi),
                });
                break;
            }

            case WM_PAINT:
            {
                PAINTSTRUCT paint_struct{};
                BeginPaint(hWnd, &paint_struct);
                use_default = false;
                std::ignore = dispatchPlatformEvent(
                    Win32::PaintEvent{paint_struct.hdc,
                                      {{paint_struct.rcPaint.left, paint_struct.rcPaint.top},
                                       {paint_struct.rcPaint.right, paint_struct.rcPaint.bottom}}},
                    return_value);
                EndPaint(hWnd, &paint_struct);
                dispatchEvent(EventPaint{});
                break;
            }

            case WM_COPYDATA:
            {
                const auto* copy_data = reinterpret_cast<const COPYDATASTRUCT*>(lParam);
                if (copy_data != nullptr)
                {
                    std::span<const std::byte> data;
                    if (copy_data->lpData != nullptr && copy_data->cbData > 0)
                    {
                        data = {static_cast<const std::byte*>(copy_data->lpData), copy_data->cbData};
                    }
                    if (dispatchPlatformEvent(Win32::CopyDataEvent{copy_data->dwData, data}, return_value))
                    {
                        use_default = false;
                    }
                }
                break;
            }

            case Win32::NotificationIconEvent::MessageId:
                if (dispatchPlatformEvent(Win32::NotificationIconEvent{LOWORD(lParam),
                                                                       static_cast<int16_t>(GET_X_LPARAM(wParam)),
                                                                       static_cast<int16_t>(GET_Y_LPARAM(wParam))},
                                          return_value))
                {
                    use_default = false;
                }
                break;

            case WM_SHOWWINDOW:
                fVisible = wParam != 0;
                break;

            case WM_GETMINMAXINFO:
                if (fMinSize.x > 0 || fMinSize.y > 0 || fMaxSize.x > 0 || fMaxSize.y > 0)
                {
                    MINMAXINFO* min_max_info = reinterpret_cast<MINMAXINFO*>(lParam);
                    const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_STYLE));
                    const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(hWnd, GWL_EXSTYLE));
                    const Size minimum = outerSizeForLogicalClient(fMinSize, style, extendedStyle, fDpi);
                    const Size maximum = outerSizeForLogicalClient(fMaxSize, style, extendedStyle, fDpi);
                    if (fMinSize.x > 0)
                        min_max_info->ptMinTrackSize.x = minimum.x;
                    if (fMinSize.y > 0)
                        min_max_info->ptMinTrackSize.y = minimum.y;
                    if (fMaxSize.x > 0)
                        min_max_info->ptMaxTrackSize.x = maximum.x;
                    if (fMaxSize.y > 0)
                        min_max_info->ptMaxTrackSize.y = maximum.y;
                    use_default = false;
                    return_value = 0;
                }
                break;

            default:
                break;
        }

        return use_default ? DefWindowProc(hWnd, message, wParam, lParam) : return_value;
    }

    LONG WindowBackendWin32::composeWindowStyles() const
    {
        if (fFullScreenState != internal::FullScreenState::Windowed)
        {
            return 0;
        }

        LONG styles = 0;
        auto window_styles = std::to_underlying(fWindowStyles);
        auto has_style = [window_styles](WindowStyle style)
        { return (window_styles & std::to_underlying(style)) != 0; };

        if (fParentBackend != nullptr)
            styles |= WS_CHILD;
        if (has_style(WindowStyle::Caption))
            styles |= WS_CAPTION;
        if (has_style(WindowStyle::CloseButton))
            styles |= WS_SYSMENU | WS_CAPTION;
        if (has_style(WindowStyle::MinimizeButton))
            styles |= WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX;
        if (has_style(WindowStyle::MaximizeButton))
            styles |= WS_SYSMENU | WS_CAPTION | WS_MAXIMIZEBOX;
        if (has_style(WindowStyle::ResizableBorder))
            styles |= WS_SIZEBOX;

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
            fFullScreenState = internal::FullScreenState::Windowed;
            return;
        }

        fFullScreenState = internal::FullScreenState::Windowed;
        updateWindowStyles();
        if (fSavedFullScreenPlacement.length == sizeof(WINDOWPLACEMENT))
        {
            // The saved rectangle is already in native coordinates for its original monitor. Applying the DPI
            // suggestion inside SetWindowPlacement would scale it a second time when restoring across monitors.
            struct RestoreScope
            {
                bool& flag;
                bool previous;
                ~RestoreScope() { flag = previous; }
            } restoreScope{fRestoringFullScreenPlacement, std::exchange(fRestoringFullScreenPlacement, true)};
            SetWindowPlacement(fHwnd, &fSavedFullScreenPlacement);
        }
    }

    void WindowBackendWin32::setFullScreen(bool multiMonitor)
    {
        if (fHwnd == nullptr)
        {
            return;
        }

        if (fFullScreenState == internal::FullScreenState::Windowed)
        {
            fSavedFullScreenPlacement.length = sizeof(WINDOWPLACEMENT);
            GetWindowPlacement(fHwnd, &fSavedFullScreenPlacement);
            const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE)) & ~(WS_MAXIMIZE | WS_MINIMIZE);
            const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
            const Size frame = outerSizeForLogicalClient({}, style, extendedStyle, fDpi);
            const RECT& normal = fSavedFullScreenPlacement.rcNormalPosition;
            fSavedWindowedClientSize = logicalFromPhysical(
                Size{normal.right - normal.left - frame.x, normal.bottom - normal.top - frame.y}, fDpi);
        }

        internal::MonitorInfo& monitor_info = fMonitors;
        monitor_info.refresh();

        RECT rect{};
        if (multiMonitor)
        {
            rect = monitor_info.getBoundingMonitorArea();
            fFullScreenState = internal::FullScreenState::MultiScreen;
        }
        else
        {
            rect = monitor_info.getMonitorInfo(MonitorFromWindow(fHwnd, MONITOR_DEFAULTTOPRIMARY)).monitorInfo.rcMonitor;
            fFullScreenState = internal::FullScreenState::SingleScreen;
        }

        updateWindowStyles();
        SetWindowPos(fHwnd, nullptr, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                     internal::WindowPosHelper::composeFlags(
                         {internal::WindowPosOp::Placement, internal::WindowPosOp::UpdateFrame}));
    }

    LRESULT WindowBackendWin32::getCorner(POINTS points) const
    {
        POINT point{points.x, points.y};
        ScreenToClient(fHwnd, &point);

        Size window_size = getWindowSize();
        auto distance_squared = [](int32_t ax, int32_t ay, int32_t bx, int32_t by) -> int64_t
        {
            int64_t dx = static_cast<int64_t>(ax) - bx;
            int64_t dy = static_cast<int64_t>(ay) - by;
            return dx * dx + dy * dy;
        };

        int64_t distances[4] = {distance_squared(point.x, point.y, 0, 0),
                                distance_squared(point.x, point.y, window_size.x, 0),
                                distance_squared(point.x, point.y, window_size.x, window_size.y),
                                distance_squared(point.x, point.y, 0, window_size.y)};

        int closest_index = 0;
        for (int index = 1; index < 4; ++index)
        {
            if (distances[index] < distances[closest_index])
            {
                closest_index = index;
            }
        }

        static constexpr LRESULT corners[4] = {HTTOPLEFT, HTTOPRIGHT, HTBOTTOMRIGHT, HTBOTTOMLEFT};
        return corners[closest_index];
    }

    bool WindowBackendWin32::dispatchPlatformEvent(const Win32::PlatformEvent& event, LRESULT& result)
    {
        return internal::WindowBackendAccess::DispatchPlatform(owner(), event, result);
    }
}  // namespace LWS
#endif  // LWS_PLATFORM_WIN32
