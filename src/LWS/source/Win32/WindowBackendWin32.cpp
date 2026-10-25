#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <WindowsX.h>

    #include <LLUtils/Exception.h>
    #include <LWS/Win32/ScopedWindowDpiContext.hpp>
    #include "internal/SharedWindowClass.hpp"
    #include "internal/BgraDib.hpp"
    #include <LWS/source/internal/BitmapPixels.hpp>
    #include <LWS/StringDefs.hpp>

    #include <algorithm>
    #include <cmath>
    #include <cstring>
    #include <limits>
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

    using DwmGetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, PVOID, DWORD);

    DwmGetWindowAttributeFn dwmAttribute()
    {
        static const auto function = []
        {
            const HMODULE module = LoadLibraryW(L"dwmapi.dll");
            return module != nullptr
                       ? reinterpret_cast<DwmGetWindowAttributeFn>(GetProcAddress(module, "DwmGetWindowAttribute"))
                       : nullptr;
        }();
        return function;
    }

    RECT visibleWindowRectangle(HWND window, RECT fallback)
    {
        RECT frame{};
        const auto attribute = dwmAttribute();
        // DWMWA_EXTENDED_FRAME_BOUNDS excludes invisible resize borders.
        if (attribute != nullptr && SUCCEEDED(attribute(window, 9, &frame, sizeof(frame))))
            return frame;
        return fallback;
    }

    struct SnapCandidates
    {
        HWND movingWindow;
        RECT moving;
        LONG threshold;
        LONG dx{};
        LONG dy{};
        LONG bestX;
        LONG bestY;
        HWND targetX{};
        HWND targetY{};
        const std::vector<HWND>* excluded{};

        void considerX(LONG delta, HWND target)
        {
            if (std::abs(delta) <= threshold && std::abs(delta) < bestX)
            {
                dx = delta;
                targetX = target;
                bestX = std::abs(delta);
            }
        }

        void considerY(LONG delta, HWND target)
        {
            if (std::abs(delta) <= threshold && std::abs(delta) < bestY)
            {
                dy = delta;
                targetY = target;
                bestY = std::abs(delta);
            }
        }

        void consider(const RECT& target, HWND window = nullptr)
        {
            const bool screen = window == nullptr;
            if (screen ? moving.top <= target.bottom && moving.bottom >= target.top
                       : moving.top < target.bottom && moving.bottom > target.top)
            {
                if (screen)
                {
                    considerX(target.left - moving.left, window);
                    considerX(target.right - moving.right, window);
                }
                else
                {
                    considerX(target.left - moving.right, window);
                    considerX(target.right - moving.left, window);
                }
            }
            if (screen ? moving.left <= target.right && moving.right >= target.left
                       : moving.left < target.right && moving.right > target.left)
            {
                if (screen)
                {
                    considerY(target.top - moving.top, window);
                    considerY(target.bottom - moving.bottom, window);
                }
                else
                {
                    considerY(target.top - moving.bottom, window);
                    considerY(target.bottom - moving.top, window);
                }
            }
        }
    };

    BOOL CALLBACK collectSnapMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM data)
    {
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info))
            reinterpret_cast<SnapCandidates*>(data)->consider(info.rcWork);
        return TRUE;
    }

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
        if (fClassRegistered)
            WindowClass().Release();
        if (fBackgroundBrush != nullptr)
        {
            DeleteObject(fBackgroundBrush);
            fBackgroundBrush = nullptr;
        }
    }

    internal::SharedWindowClass& WindowBackendWin32::WindowClass()
    {
        static internal::SharedWindowClass windowClass{g_windowClassName, WndProc, CS_DBLCLKS,
                                                       LoadCursorW(nullptr, IDC_ARROW)};
        return windowClass;
    }

    Result WindowBackendWin32::create(const internal::NativeWindowConfig& config)
    {
        if (fHwnd != nullptr)
        {
            return Result::AlreadyCreated;
        }

        if (!fClassRegistered)
        {
            if (!WindowClass().Acquire())
                return Result::Failure;
            fClassRegistered = true;
        }
        if (config.externalParent != 0)
            fParentWindow = reinterpret_cast<HWND>(config.externalParent);
        // Adopt the external parent's DPI context only for native creation. LWS owns this child, so
        // its normal events provide geometry and lifetime without observing or borrowing the host.
        const LWS::Win32::ScopedWindowDpiContext dpiContext(config.externalParent != 0 ? fParentWindow : nullptr);

        const HINSTANCE instance = WindowClass().Module();

        fEraseBackground = config.eraseBackground;
        fAlwaysOnTop = config.alwaysOnTop;
        fTransparent = config.transparent;
        fMinSize = config.minSize;
        fMaxSize = config.maxSize;
        fWindowStyles = config.styles;
        fSnappingMode = config.snappingMode;
        fSnappingRole = config.snappingRole;
        fPublishedSnappingState = {fSnappingRole};
        fDisplayState = config.displayState;
        fVisible = config.visible;
        fLastMousePos = {};
        fPopupPlacement = config.popup;
        updateBackgroundBrush();

        DWORD ex_style = 0;
        if (fPopupPlacement.has_value())
        {
            ex_style |= WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE;
        }
        if (fAlwaysOnTop)
        {
            ex_style |= WS_EX_TOPMOST;
        }
        if (fTransparent && fParentWindow == nullptr)
        {
            ex_style |= WS_EX_LAYERED;
        }

        const UINT dpi = fParentWindow != nullptr ? windowDpi(fParentWindow) : systemDpi();
        // Popup placement resolves before the native window exists; WM_CREATE refreshes this cache afterwards.
        fDpi = dpi;
        DWORD style = 0;
        Point position{};
        Size outerSize{};
        if (fPopupPlacement.has_value())
        {
            // An owned WS_POPUP top-level tracks its owner's z-order without ever activating.
            style = static_cast<DWORD>(WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS);
            outerSize = outerSizeForLogicalClient(config.size, style, ex_style, dpi);
            position = resolvePopupPosition(outerSize);
        }
        else
        {
            style = static_cast<DWORD>(WS_CLIPCHILDREN | WS_CLIPSIBLINGS | composeWindowStyles());
            position = physicalFromLogical(config.position, dpi);
            outerSize = outerSizeForLogicalClient(config.size, style, ex_style, dpi);
        }
        HWND hwnd = CreateWindowEx(ex_style, g_windowClassName, config.title.c_str(), style, position.x, position.y,
                                   outerSize.x, outerSize.y, fParentWindow, nullptr, instance, this);

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

        if (fTransparent && fParentWindow != nullptr)
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
            ShowWindow(fHwnd, fPopupPlacement.has_value() ? SW_SHOWNOACTIVATE : SW_SHOW);
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
        fPopupPlacement.reset();
    }

    void WindowBackendWin32::show()
    {
        fVisible = true;
        if (fHwnd != nullptr)
        {
            ShowWindow(fHwnd, fPopupPlacement.has_value() ? SW_SHOWNOACTIVATE : SW_SHOW);
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
            {
                const POINT offset = internal::WindowPosHelper::workspaceOffset(monitor, extendedStyle);
                OffsetRect(&bounds, -offset.x, -offset.y);
            }
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

    Result WindowBackendWin32::setWindowIcon(const BitmapBuffer* icon, bool reuseResource)
    {
        if (icon == nullptr)
        {
            const HICON previousIcon = std::exchange(fWindowIcon, nullptr);
            applyWindowIcon();
            if (previousIcon != nullptr)
                DestroyIcon(previousIcon);
            return Result::Success;
        }
        if (reuseResource && fWindowIcon != nullptr)
        {
            // Window has retained the same immutable icon. Reapply its handles without rebuilding bitmap objects.
            applyWindowIcon();
            return Result::Success;
        }
        const auto layout = internal::validateBitmapBuffer(*icon);
        if (!layout.has_value() || icon->format != BitmapPixelFormat::Bgra8Premultiplied)
            return Result::InvalidArgument;

        // The legacy AND mask must be deterministic even when every alpha byte is zero.
        const size_t maskPitch = (size_t(icon->width) + 15) / 16 * 2;
        std::vector<std::byte> maskBits;
        try
        {
            maskBits.resize(maskPitch * icon->height, std::byte{0});
        }
        catch (const std::bad_alloc&)
        {
            return Result::Failure;
        }
        for (uint32_t y = 0; y < icon->height; ++y)
            for (uint32_t x = 0; x < icon->width; ++x)
                if (icon->pixels[size_t(y) * icon->rowPitch + size_t(x) * 4 + 3] == std::byte{0})
                    maskBits[size_t(icon->height - 1 - y) * maskPitch + x / 8] |= std::byte(0x80U >> (x % 8));
        void* targetPixels{};
        HBITMAP color = internal::createBgraDib(icon->width, icon->height, targetPixels);
        if (color == nullptr || targetPixels == nullptr)
            return Result::Failure;
        const size_t rowBytes = static_cast<size_t>(icon->width) * 4U;
        for (uint32_t row = 0; row < icon->height; ++row)
            std::memcpy(static_cast<std::byte*>(targetPixels) + row * rowBytes,
                        icon->pixels.data() + row * icon->rowPitch, rowBytes);
        HBITMAP mask = CreateBitmap(static_cast<int>(icon->width), static_cast<int>(icon->height), 1, 1,
                                    maskBits.data());
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
        if (fPopupPlacement.has_value())
        {
            fPopupPlacement->anchor = pos;
            applyPopupPlacement();
            return;
        }
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
        // WM_MOVE can precede WM_SIZE, so use native state rather than the last published show state.
        if (fParentWindow != nullptr || (GetWindowLongPtrW(fHwnd, GWL_STYLE) & (WS_MINIMIZE | WS_MAXIMIZE)) == 0)
        {
            // Restored top-level geometry is already in screen coordinates.
            RECT rectangle{};
            GetWindowRect(fHwnd, &rectangle);
            position = {rectangle.left, rectangle.top};
            if (fParentWindow != nullptr)
                ScreenToClient(fParentWindow, &position);
        }
        else
        {
            WINDOWPLACEMENT placement{};
            placement.length = sizeof(placement);
            GetWindowPlacement(fHwnd, &placement);
            position = {placement.rcNormalPosition.left, placement.rcNormalPosition.top};
            const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if ((extendedStyle & WS_EX_TOOLWINDOW) == 0 &&
                GetMonitorInfoW(MonitorFromWindow(fHwnd, MONITOR_DEFAULTTONEAREST), &monitor))
            {
                const POINT offset = internal::WindowPosHelper::workspaceOffset(monitor, extendedStyle);
                position.x += offset.x;
                position.y += offset.y;
            }
        }
        return logicalFromPhysical(Point{position.x, position.y}, fDpi);
    }

    void WindowBackendWin32::setSize(Size sz)
    {
        if (fPopupPlacement.has_value())
        {
            fPopupPlacement->size = {sz.x, sz.y};
            applyPopupPlacement();
            return;
        }
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
        if (fPopupPlacement.has_value())
        {
            fPopupPlacement->anchor = placement.position;
            fPopupPlacement->size = {placement.size.x, placement.size.y};
            applyPopupPlacement();
            return;
        }
        if (fHwnd == nullptr)
        {
            return;
        }

        const Point position = physicalFromLogical(placement.position, fDpi);
        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
        const Size outerSize = outerSizeForLogicalClient(placement.size, style, extendedStyle, fDpi);
        internal::WindowPosHelper::setPlacement(fHwnd, position.x, position.y, outerSize.x, outerSize.y);
    }

    Point WindowBackendWin32::resolvePopupPosition(Size outerSize) const
    {
        POINT origin{0, 0};
        ClientToScreen(fParentWindow, &origin);
        const Point anchorPoint{fPopupPlacement->anchor.x + fPopupPlacement->offset.x,
                                fPopupPlacement->anchor.y + fPopupPlacement->offset.y};
        const Point anchor = physicalFromLogical(anchorPoint, fDpi);
        const int32_t screenAnchorX = origin.x + anchor.x;
        const int32_t screenAnchorY = origin.y + anchor.y;
        const bool extendsRight = fPopupPlacement->gravity == PopupGravity::DownRight ||
                                  fPopupPlacement->gravity == PopupGravity::UpRight;
        const bool extendsDown = fPopupPlacement->gravity == PopupGravity::DownRight ||
                                 fPopupPlacement->gravity == PopupGravity::DownLeft;
        Point position{extendsRight ? screenAnchorX : screenAnchorX - outerSize.x,
                       extendsDown ? screenAnchorY : screenAnchorY - outerSize.y};

        if (fPopupPlacement->adjustToScreen)
        {
            MONITORINFO monitor{sizeof(MONITORINFO)};
            if (GetMonitorInfoW(MonitorFromPoint(POINT{screenAnchorX, screenAnchorY}, MONITOR_DEFAULTTONEAREST),
                                &monitor))
            {
                const RECT& work = monitor.rcWork;
                // Flip toward the opposite side first, then clamp to the work area for popups larger than it.
                if (extendsRight && position.x + outerSize.x > work.right && screenAnchorX - outerSize.x >= work.left)
                    position.x = screenAnchorX - outerSize.x;
                else if (!extendsRight && position.x < work.left && screenAnchorX + outerSize.x <= work.right)
                    position.x = screenAnchorX;
                if (extendsDown && position.y + outerSize.y > work.bottom && screenAnchorY - outerSize.y >= work.top)
                    position.y = screenAnchorY - outerSize.y;
                else if (!extendsDown && position.y < work.top && screenAnchorY + outerSize.y <= work.bottom)
                    position.y = screenAnchorY;
                const int32_t workLeft = work.left;
                const int32_t workTop = work.top;
                const int32_t maxX = std::max(workLeft, static_cast<int32_t>(work.right) - outerSize.x);
                const int32_t maxY = std::max(workTop, static_cast<int32_t>(work.bottom) - outerSize.y);
                position.x = std::clamp(position.x, workLeft, maxX);
                position.y = std::clamp(position.y, workTop, maxY);
            }
        }
        return position;
    }

    void WindowBackendWin32::applyPopupPlacement()
    {
        if (fHwnd == nullptr || !fPopupPlacement.has_value())
        {
            return;
        }

        const DWORD style = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_STYLE));
        const DWORD extendedStyle = static_cast<DWORD>(GetWindowLongPtrW(fHwnd, GWL_EXSTYLE));
        const Size outerSize = outerSizeForLogicalClient(Size{fPopupPlacement->size.x, fPopupPlacement->size.y}, style,
                                                         extendedStyle, fDpi);
        const Point position = resolvePopupPosition(outerSize);
        internal::WindowPosHelper::setPlacement(fHwnd, position.x, position.y, outerSize.x, outerSize.y);
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

    void WindowBackendWin32::setWindowStyles(WindowStyle styles)
    {
        fWindowStyles = styles;
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
        const internal::WindowBackendAccess::DispatchScope dispatch(owner());
        clearAttachments();
        if (fHwnd == nullptr)
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

    Result WindowBackendWin32::beginWindowDrag(WindowDragOperation operation)
    {
        if (fHwnd != nullptr)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor) != FALSE)
            {
                const LPARAM position = MAKELPARAM(cursor.x, cursor.y);
                const LRESULT hitTest = operation == WindowDragOperation::Move ? HTCAPTION
                                                                               : getCorner(MAKEPOINTS(position));
                ReleaseCapture();
                SendMessageW(fHwnd, WM_NCLBUTTONDOWN, hitTest, position);
            }
        }
        return Result::Success;
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
        fParentWindow = parent != nullptr ? reinterpret_cast<HWND>(parent->getHandle()) : nullptr;
    }

    Result WindowBackendWin32::ValidateExternalParent(HWND parent)
    {
        // Use ordinary managed parenting for LWS windows so family teardown cannot be bypassed.
        return IsWindow(parent) && GetWindowThreadProcessId(parent, nullptr) == GetCurrentThreadId() &&
                       GetPropW(parent, g_windowPropName) == nullptr
                   ? Result::Success
                   : Result::InvalidArgument;
    }

    Result WindowBackendWin32::reparent(internal::IWindowBackend& parent)
    {
        const HWND handle = reinterpret_cast<HWND>(parent.getHandle());
        if (handle == fHwnd || IsChild(fHwnd, handle))
            return Result::InvalidArgument;
        const HWND previous = std::exchange(fParentWindow, handle);
        SetLastError(ERROR_SUCCESS);
        if (::SetParent(fHwnd, handle) == nullptr && GetLastError() != ERROR_SUCCESS)
        {
            fParentWindow = previous;
            return Result::Failure;
        }
        fDpi = windowDpi(fHwnd);
        dispatchClientAreaSizeChanged(getFramebufferSize());
        return Result::Success;
    }

    void WindowBackendWin32::dispatchClientAreaSizeChanged(Size framebufferSize)
    {
        const double scale = static_cast<double>(fDpi) / 96.0;
        const Size logicalSize{static_cast<int32_t>(std::lround(framebufferSize.x / scale)),
                               static_cast<int32_t>(std::lround(framebufferSize.y / scale))};
        std::ignore = dispatchEvent(EventClientAreaSizeChanged{
            {{logicalSize.x, logicalSize.y}, PixelSize{framebufferSize.x, framebufferSize.y}}});
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

        auto dragAndDrop = std::shared_ptr<internal::DragAndDropTarget>(
            new internal::DragAndDropTarget(fHwnd, [this](const std::filesystem::path& path)
                                            { dispatchEvent(EventDragDropFile{path}); }),
            [](internal::DragAndDropTarget* target) { target->Release(); });
        if (FAILED(dragAndDrop->getAttachResult()))
        {
            fDndEnabled = false;
            return Result::Failure;
        }

        fDragAndDrop = std::move(dragAndDrop);
        return Result::Success;
    }

    bool WindowBackendWin32::drawBitmap(HDC dc) const
    {
        if ((!fPresentedBitmap && !fPresentedPixels) || !dc)
            return false;
        const auto bitmap = fPresentedPixels ? BitmapBuffer{{fPresentedPixels.get(), fPresentedPixelSize},
                                                            BitmapPixelFormat::Bgra8Premultiplied,
                                                            BitmapRowOrder::TopDown,
                                                            fPresentedWidth,
                                                            fPresentedHeight,
                                                            fPresentedWidth * 4U}
                                             : fPresentedBitmap->GetBuffer();
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = static_cast<LONG>(bitmap.width);
        info.bmiHeader.biHeight = -static_cast<LONG>(bitmap.height);
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        RECT rect{};
        GetClientRect(fHwnd, &rect);
        const int result = StretchDIBits(dc, 0, 0, rect.right, rect.bottom, 0, 0, bitmap.width, bitmap.height,
                                         bitmap.pixels.data(), &info, DIB_RGB_COLORS, SRCCOPY);
        return result != GDI_ERROR && result != 0;
    }

    Result WindowBackendWin32::presentBitmap(const BitmapBuffer& bitmap)
    {
        if (!fHwnd)
            return Result::InvalidState;
        const auto layout = internal::validateBitmapBuffer(bitmap);
        if (!layout || bitmap.width > LONG_MAX / 4 || bitmap.height > LONG_MAX)
            return Result::Failure;
        try
        {
            // Keep an owned snapshot for WM_PAINT. The common UI format can reuse
            // its storage, while other formats retain Bitmap's conversion behavior.
            if (bitmap.format == BitmapPixelFormat::Bgra8Premultiplied)
            {
                if (static_cast<size_t>(bitmap.width) > std::numeric_limits<size_t>::max() / 4U / bitmap.height)
                    return Result::Failure;
                const size_t rowSize = static_cast<size_t>(bitmap.width) * 4U;
                const size_t bytes = rowSize * bitmap.height;
                if (bytes != fPresentedPixelSize)
                    fPresentedPixels = std::make_unique_for_overwrite<std::byte[]>(bytes);
                internal::copyBitmapPixels(bitmap, *layout, fPresentedPixels.get());
                fPresentedPixelSize = bytes;
                fPresentedWidth = bitmap.width;
                fPresentedHeight = bitmap.height;
                fPresentedBitmap.reset();
            }
            else
            {
                auto converted = std::make_unique<Bitmap>(bitmap);
                fPresentedBitmap = std::move(converted);
                fPresentedPixels.reset();
                fPresentedPixelSize = 0;
            }
        }
        catch (const std::invalid_argument&)
        {
            return Result::Failure;
        }
        catch (const std::bad_alloc&)
        {
            return Result::Failure;
        }
        HDC dc = GetDC(fHwnd);
        bool drawn = drawBitmap(dc);
        if (dc)
            ReleaseDC(fHwnd, dc);
        return drawn ? Result::Success : Result::Failure;
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
            self->fSnappingLifetime = std::make_shared<SnappingLifetime>(SnappingLifetime{self});
            self->fDestroyingSnapping = false;
            self->fDpi = windowDpi(hWnd);
        }

        WindowBackendWin32* self = reinterpret_cast<WindowBackendWin32*>(GetProp(hWnd, g_windowPropName));
        if (self != nullptr)
        {
            const internal::WindowBackendAccess::DispatchScope dispatch(self->owner());
            return self->windowProc(hWnd, message, wParam, lParam);
        }

        return DefWindowProc(hWnd, message, wParam, lParam);
    }

    WindowBackendWin32::SnappingReference WindowBackendWin32::snappingReference(HWND window)
    {
        DWORD process{};
        if (window == nullptr || GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() ||
            process != GetCurrentProcessId())
            return {};
        auto* backend = static_cast<WindowBackendWin32*>(GetPropW(window, g_windowPropName));
        return backend != nullptr ? backend->snappingReference() : SnappingReference{};
    }

    BOOL CALLBACK WindowBackendWin32::collectSnapWindow(HWND window, LPARAM data)
    {
        auto& candidates = *reinterpret_cast<SnapCandidates*>(data);
        if (window == candidates.movingWindow ||
            (candidates.excluded != nullptr &&
             std::ranges::find(*candidates.excluded, window) != candidates.excluded->end()) ||
            !IsWindowVisible(window) || IsIconic(window) ||
            (GetWindowLongW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0)
            return TRUE;
        auto* moving = snappingReference(candidates.movingWindow).get();
        auto* target = snappingReference(window).get();
        if (moving != nullptr && target != nullptr &&
            &moving->owner().GetPlatformContext() == &target->owner().GetPlatformContext() &&
            target->fSnappingMode != WindowSnappingMode::OtherWindows &&
            target->fSnappingMode != WindowSnappingMode::ScreenEdgesAndOtherWindows)
            return TRUE;
        DWORD cloaked{};
        const auto attribute = dwmAttribute();
        // DWMWA_CLOAKED excludes windows on inactive virtual desktops.
        if (attribute != nullptr && SUCCEEDED(attribute(window, 14, &cloaked, sizeof(cloaked))) && cloaked != 0)
            return TRUE;
        RECT rectangle{};
        if (GetWindowRect(window, &rectangle))
            candidates.consider(visibleWindowRectangle(window, rectangle), window);
        return TRUE;
    }

    bool WindowBackendWin32::allowsWindowSnapping() const
    {
        return !fDestroyingSnapping && owner().IsCreated() && fParentWindow == nullptr && !fPopupPlacement &&
               fFullScreenState == internal::FullScreenState::Windowed && IsWindowVisible(fHwnd) && !IsIconic(fHwnd) &&
               !IsZoomed(fHwnd) &&
               (fSnappingMode == WindowSnappingMode::OtherWindows ||
                fSnappingMode == WindowSnappingMode::ScreenEdgesAndOtherWindows);
    }

    WindowSnappingState WindowBackendWin32::getSnappingState() const
    {
        return {fSnappingRole, fSnappingLeader.get() != nullptr, fSnappingFollowers.size()};
    }

    void WindowBackendWin32::notifySnappingState()
    {
        const auto state = getSnappingState();
        if (state == fPublishedSnappingState)
            return;
        fPublishedSnappingState = state;
        if (!fDestroyingSnapping && owner().IsCreated())
            std::ignore = dispatchEvent(EventSnappingStateChanged{});
    }

    void WindowBackendWin32::clearAttachments(bool cancelDrag)
    {
        ++fAttachmentRevision;
        if (cancelDrag && fInSnappingDrag)
            fAttachmentCancelled = true;
        fPendingAttachments.clear();
        const auto self = snappingReference();
        std::vector<SnappingReference> changed;
        if (auto* leader = fSnappingLeader.get())
        {
            std::erase(leader->fSnappingFollowers, self);
            changed.push_back(fSnappingLeader);
        }
        fSnappingLeader = {};
        for (const auto& follower : std::exchange(fSnappingFollowers, {}))
            if (auto* peer = follower.get())
            {
                peer->fSnappingLeader = {};
                ++peer->fAttachmentRevision;
                peer->fPendingAttachments.clear();
                if (cancelDrag && peer->fInSnappingDrag)
                    peer->fAttachmentCancelled = true;
                changed.push_back(follower);
            }
        changed.push_back(self);
        // Publish only after all reciprocal relationships are consistent. Callbacks can retire any participant.
        for (const auto& reference : changed)
            if (auto* peer = reference.get())
                peer->notifySnappingState();
    }

    void WindowBackendWin32::releaseSnappingAttachments()
    {
        clearAttachments();
    }

    void WindowBackendWin32::setSnappingRole(WindowSnappingRole role)
    {
        if (role == fSnappingRole)
            return;
        fSnappingRole = role;
        clearAttachments();
    }

    void WindowBackendWin32::setSnappingMode(WindowSnappingMode mode)
    {
        if (mode == fSnappingMode)
            return;
        fSnappingDragAnchor.Reset();
        fSnappingMode = mode;
        if (mode != WindowSnappingMode::OtherWindows && mode != WindowSnappingMode::ScreenEdgesAndOtherWindows)
            clearAttachments();
    }

    void WindowBackendWin32::detachForMovement()
    {
        if (fSnappingLeader.get() == nullptr)
            return;
        clearAttachments(false);
    }

    void WindowBackendWin32::commitSnapAttachments()
    {
        const auto pending = std::exchange(fPendingAttachments, {});
        const auto self = snappingReference();
        const auto revision = fAttachmentRevision;
        for (const auto& reference : pending)
        {
            if (self.get() != this || revision != fAttachmentRevision || fAttachmentCancelled ||
                fSnappingRole == WindowSnappingRole::Independent || !allowsWindowSnapping() ||
                (fSnappingRole == WindowSnappingRole::Follower && fSnappingLeader.get() != nullptr))
                break;
            auto* peer = reference.get();
            if (peer == nullptr || peer == this || !peer->allowsWindowSnapping() ||
                &peer->owner().GetPlatformContext() != &owner().GetPlatformContext())
                continue;
            auto* leader = fSnappingRole == WindowSnappingRole::Leader ? this : peer;
            auto* follower = fSnappingRole == WindowSnappingRole::Follower ? this : peer;
            if (leader->fSnappingRole != WindowSnappingRole::Leader ||
                follower->fSnappingRole != WindowSnappingRole::Follower || follower->fSnappingLeader.get() != nullptr)
                continue;
            RECT outer{}, targetOuter{};
            if (!GetWindowRect(fHwnd, &outer) || !GetWindowRect(peer->fHwnd, &targetOuter))
                continue;
            const RECT frame = visibleWindowRectangle(fHwnd, outer);
            const RECT other = visibleWindowRectangle(peer->fHwnd, targetOuter);
            const bool side = (std::abs(frame.left - other.right) <= 1 || std::abs(frame.right - other.left) <= 1) &&
                              frame.top < other.bottom && frame.bottom > other.top;
            const bool above = (std::abs(frame.top - other.bottom) <= 1 || std::abs(frame.bottom - other.top) <= 1) &&
                               frame.left < other.right && frame.right > other.left;
            if (side || above)
            {
                leader->fSnappingFollowers.push_back(follower->snappingReference());
                follower->fSnappingLeader = leader->snappingReference();
                notifySnappingState();
                if (auto* current = reference.get())
                    current->notifySnappingState();
            }
        }
    }

    void WindowBackendWin32::moveSnappingFollowers()
    {
        RECT outer{};
        if (!GetWindowRect(fHwnd, &outer))
            return;
        const POINT position{outer.left, outer.top};
        const auto previous = std::exchange(fSnappingPosition, position);
        if (fSnappingMoveTarget)
        {
            if (position.x != fSnappingMoveTarget->x || position.y != fSnappingMoveTarget->y)
                clearAttachments();
            return;
        }
        if (!previous || (position.x == previous->x && position.y == previous->y))
            return;
        if (fSnappingLeader.get() != nullptr)
        {
            detachForMovement();
            return;
        }
        if (fSnappingFollowers.empty())
            return;
        const auto self = snappingReference();
        const auto revision = fAttachmentRevision;
        const LONG dx = position.x - previous->x, dy = position.y - previous->y;
        struct Member
        {
            SnappingReference reference;
            RECT rectangle;
        };
        std::vector<Member> members;
        for (const auto& reference : fSnappingFollowers)
        {
            auto* peer = reference.get();
            RECT rectangle{};
            if (peer != nullptr && GetWindowRect(peer->fHwnd, &rectangle))
                members.push_back({reference, rectangle});
        }
        fSnappingMoveTarget = position;
        for (const auto& member : members)
        {
            if (self.get() != this || revision != fAttachmentRevision)
                break;
            auto* peer = member.reference.get();
            if (peer == nullptr || peer->fSnappingLeader != self)
                continue;
            RECT current{};
            if (!GetWindowRect(peer->fHwnd, &current) || !EqualRect(&current, &member.rectangle))
            {
                peer->clearAttachments();
                continue;
            }
            const POINT expected{member.rectangle.left + dx, member.rectangle.top + dy};
            peer->fSnappingMoveTarget = expected;
            const BOOL moved = SetWindowPos(peer->fHwnd, nullptr, expected.x, expected.y, 0, 0,
                                            SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);
            peer = member.reference.get();
            if (peer != nullptr)
            {
                peer->fSnappingMoveTarget.reset();
                if (peer->fSnappingLeader == self &&
                    (!moved || !GetWindowRect(peer->fHwnd, &current) || current.left != expected.x ||
                     current.top != expected.y ||
                     current.right - current.left != member.rectangle.right - member.rectangle.left ||
                     current.bottom - current.top != member.rectangle.bottom - member.rectangle.top))
                    peer->clearAttachments();
            }
        }
        if (self.get() == this)
            fSnappingMoveTarget.reset();
    }

    void WindowBackendWin32::snapMovingRectangle(RECT& rectangle)
    {
        fPendingAttachments.clear();
        RECT outer{};
        if (!GetWindowRect(fHwnd, &outer))
            return;
        const auto self = snappingReference();
        if (fSnappingLeader.get() != nullptr && (rectangle.left != outer.left || rectangle.top != outer.top))
        {
            detachForMovement();
            if (self.get() != this)
                return;
            RECT current{};
            if (!GetWindowRect(fHwnd, &current))
                return;
            if (!EqualRect(&current, &outer))
            {
                rectangle = current;
                return;
            }
        }
        const RECT frame = visibleWindowRectangle(fHwnd, outer);
        const RECT proposedFrame{rectangle.left + frame.left - outer.left, rectangle.top + frame.top - outer.top,
                                 rectangle.right + frame.right - outer.right,
                                 rectangle.bottom + frame.bottom - outer.bottom};
        const LONG threshold = std::max(1, physicalFromLogical(12, fDpi));
        std::vector<HWND> excluded;
        for (const auto& reference : fSnappingFollowers)
            if (auto* peer = reference.get())
                excluded.push_back(peer->fHwnd);
        SnapCandidates candidates{fHwnd, proposedFrame, threshold, 0, 0, threshold + 1, threshold + 1};
        candidates.excluded = &excluded;
        if (fSnappingMode == WindowSnappingMode::ScreenEdges ||
            fSnappingMode == WindowSnappingMode::ScreenEdgesAndOtherWindows)
            EnumDisplayMonitors(nullptr, nullptr, collectSnapMonitor, reinterpret_cast<LPARAM>(&candidates));
        if (fSnappingMode == WindowSnappingMode::OtherWindows ||
            fSnappingMode == WindowSnappingMode::ScreenEdgesAndOtherWindows)
            EnumWindows(collectSnapWindow, reinterpret_cast<LPARAM>(&candidates));
        OffsetRect(&rectangle, candidates.dx, candidates.dy);
        if (fSnappingRole != WindowSnappingRole::Independent && !fAttachmentCancelled)
            for (const HWND target : {candidates.targetX, candidates.targetY})
            {
                const auto reference = snappingReference(target);
                if (reference.get() != nullptr &&
                    std::ranges::find(fPendingAttachments, reference) == fPendingAttachments.end())
                    fPendingAttachments.push_back(reference);
            }
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
                                                          true, message},
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
                                                          false, message},
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
                if (dispatchEvent(EventCloseRequested{}) == EventResponse::Unhandled && owner().IsCreated())
                    std::ignore = owner().Destroy();
                use_default = false;
                break;

            case WM_DESTROY:
                internal::WindowBackendAccess::NativeDestroying(owner());
                fSnappingDragAnchor.Reset();
                fDestroyingSnapping = true;
                clearAttachments();
                if (fSnappingLifetime)
                    fSnappingLifetime->backend = nullptr;
                fSnappingLifetime.reset();
                fSnappingPosition.reset();
                fSnappingSize.reset();
                fPresentedBitmap.reset();
                fPresentedPixels.reset();
                fPresentedPixelSize = 0;
                fPendingHighSurrogate = 0;
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
                if (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED)
                    clearAttachments();
                if (fHwnd == nullptr)
                    break;
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

                Size framebufferSize{static_cast<int32_t>(LOWORD(lParam)), static_cast<int32_t>(HIWORD(lParam))};
                if (new_state != fDisplayState)
                {
                    fDisplayState = new_state;
                    std::ignore = dispatchEvent(EventShowStateChanged{fDisplayState});
                    if (fHwnd == nullptr)
                        break;
                    // A state listener can synchronously resize, restore, or minimize the native window.
                    // Observe its final geometry instead of replaying the outer WM_SIZE dimensions.
                    framebufferSize = fDisplayState == WindowShowState::Minimized ? Size{} : getFramebufferSize();
                }

                dispatchClientAreaSizeChanged(framebufferSize);
                break;
            }

            case WM_DPICHANGED:
                fDpi = LOWORD(wParam);
                if (fParentWindow == nullptr)
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

            case WM_ENTERSIZEMOVE:
                fSnappingDragAnchor.Reset();
                fPendingAttachments.clear();
                fInSnappingDrag = true;
                fAttachmentCancelled = false;
                break;

            case WM_EXITSIZEMOVE:
                fSnappingDragAnchor.Reset();
                commitSnapAttachments();
                fInSnappingDrag = false;
                fAttachmentCancelled = false;
                break;

            case WM_WINDOWPOSCHANGED:
            {
                const auto* placement = reinterpret_cast<const WINDOWPOS*>(lParam);
                RECT current{};
                if (GetWindowRect(hWnd, &current))
                {
                    const Size size{current.right - current.left, current.bottom - current.top};
                    const auto previousSize = std::exchange(fSnappingSize, size);
                    if ((placement->flags & SWP_HIDEWINDOW) != 0 || IsIconic(hWnd) || IsZoomed(hWnd) ||
                        (previousSize && (size.x != previousSize->x || size.y != previousSize->y)))
                        clearAttachments();
                }
                break;
            }

            case WM_MOVING:
                if (fSnappingMode != WindowSnappingMode::Disabled && fParentWindow == nullptr &&
                    fFullScreenState == internal::FullScreenState::Windowed && lParam != 0)
                {
                    auto& rectangle = *reinterpret_cast<RECT*>(lParam);
                    if (fInSnappingDrag && (fSnappingDragAnchor.Active() || GetCapture() == hWnd ||
                                            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0))
                    {
                        POINT cursor{};
                        if (GetCursorPos(&cursor))
                            fSnappingDragAnchor.Apply(rectangle, cursor);
                        else
                            fSnappingDragAnchor.Reset();
                    }
                    snapMovingRectangle(rectangle);
                    use_default = false;
                    return_value = TRUE;
                }
                break;

            case WM_MOVE:
                moveSnappingFollowers();
                if (fHwnd != nullptr)
                    dispatchEvent(EventMove{getPosition()});
                break;

            case WM_TIMER:
                internal::dispatchWindowTimer(hWnd, wParam);
                use_default = false;
                break;

            case WM_SETFOCUS:
                dispatchEvent(EventFocusGained{});
                break;

            case WM_KILLFOCUS:
                fPendingHighSurrogate = 0;
                dispatchEvent(EventFocusLost{});
                break;

            case WM_ACTIVATE:
                if (dispatchPlatformEvent(Win32::ActivationEvent{LOWORD(wParam) != WA_INACTIVE}, return_value))
                {
                    use_default = false;
                }
                break;

            case WM_MOUSEACTIVATE:
                if (fPopupPlacement.has_value())
                {
                    // Clicking a popup must not steal focus from the window whose menu it belongs to.
                    use_default = false;
                    return_value = MA_NOACTIVATE;
                }
                break;

            case WM_CAPTURECHANGED:
                if (reinterpret_cast<HWND>(lParam) != fHwnd)
                    dispatchEvent(EventMouseCaptureLost{});
                break;

            case WM_MOUSELEAVE:
                dispatchEvent(EventMouseLeave{});
                break;

            case WM_CHAR:
            {
                wchar_t units[2]{};
                int count = 1;
                wchar_t ch = static_cast<wchar_t>(wParam);
                if (ch >= 0xd800 && ch <= 0xdbff)
                {
                    fPendingHighSurrogate = ch;
                    break;
                }
                if (ch >= 0xdc00 && ch <= 0xdfff)
                {
                    if (!fPendingHighSurrogate)
                        break;
                    units[0] = fPendingHighSurrogate;
                    units[1] = ch;
                    count = 2;
                }
                else
                    units[0] = ch;
                fPendingHighSurrogate = 0;
                if (ch < 32 || ch == 127)
                    break;
                char bytes[8]{};
                int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, units, count, bytes, sizeof(bytes),
                                               nullptr, nullptr);
                if (size > 0)
                {
                    const unsigned repeats = std::max(1U, unsigned(LOWORD(lParam)));
                    std::string text;
                    text.reserve(size_t(size) * repeats);
                    for (unsigned i = 0; i < repeats; ++i)
                        text.append(bytes, size);
                    dispatchEvent(EventTextInput{std::move(text)});
                }
                use_default = false;
                break;
            }

            case WM_MOUSEMOVE:
            {
                TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, hWnd, 0};
                TrackMouseEvent(&tracking);
                const Point position = logicalFromPhysical(Point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, fDpi);
                Point delta{position.x - fLastMousePos.x, position.y - fLastMousePos.y};
                fLastMousePos = position;
                dispatchEvent(EventMouseMove{position, delta});
                break;
            }

            case WM_NCLBUTTONDBLCLK:
                if (wParam == HTCAPTION)
                {
                    POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                    RECT client{};
                    if (ScreenToClient(hWnd, &point) && GetClientRect(hWnd, &client) && PtInRect(&client, point))
                    {
                        // Native dragging may return the next press through the caption path. A custom
                        // client caption owns this gesture; the real non-client caption keeps its default.
                        use_default = false;
                        const auto position = logicalFromPhysical(Point{point.x, point.y}, fDpi);
                        dispatchEvent(EventMouseButton{MouseButton::Left, true, position, 2});
                    }
                }
                break;

            case WM_LBUTTONDBLCLK:
            case WM_LBUTTONDOWN:
            case WM_LBUTTONUP:
            case WM_RBUTTONDBLCLK:
            case WM_RBUTTONDOWN:
            case WM_RBUTTONUP:
            case WM_MBUTTONDBLCLK:
            case WM_MBUTTONDOWN:
            case WM_MBUTTONUP:
            case WM_XBUTTONDBLCLK:
            case WM_XBUTTONDOWN:
            case WM_XBUTTONUP:
            {
                const bool doubleClick = message == WM_LBUTTONDBLCLK || message == WM_RBUTTONDBLCLK ||
                                         message == WM_MBUTTONDBLCLK || message == WM_XBUTTONDBLCLK;
                bool pressed = doubleClick || message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN ||
                               message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
                MouseButton button = MouseButton::Left;
                if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP || message == WM_RBUTTONDBLCLK)
                {
                    button = MouseButton::Right;
                }
                else if (message == WM_MBUTTONDOWN || message == WM_MBUTTONUP || message == WM_MBUTTONDBLCLK)
                {
                    button = MouseButton::Middle;
                }
                else if (message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK)
                {
                    button = GET_XBUTTON_WPARAM(wParam) == XBUTTON1 ? MouseButton::X1 : MouseButton::X2;
                }

                const Point position = logicalFromPhysical(Point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)}, fDpi);
                dispatchEvent(EventMouseButton{button, pressed, position, doubleClick ? 2U : 1U});
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
                if (fHwnd)
                    std::ignore = drawBitmap(paint_struct.hdc);
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

        if (fParentWindow != nullptr && !fPopupPlacement.has_value())
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

        // CreateWindowEx adds a caption to overlapped windows. Captionless top-levels
        // need WS_POPUP even when they are ordinary activating windows, not menu popups.
        if ((styles & (WS_CHILD | WS_CAPTION)) == 0)
            styles |= WS_POPUP;

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
                     internal::WindowPosHelper::PlacementFlags | SWP_FRAMECHANGED);
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
