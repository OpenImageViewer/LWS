#ifdef LWS_HAS_WIN32_BACKEND
    #include <catch2/catch_test_macros.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <array>
    #include <algorithm>
    #include <cmath>
    #include <string>
    #include <thread>
    #include <future>
    #include <commctrl.h>

TEST_CASE("Child DPI is current before parent layout", "[dpi][window][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    for (bool increasing : {false, true})
    {
        CAPTURE(increasing);
        LWS::Window parent(platform), child(platform), grandchild(platform);
        REQUIRE(parent.Create({.clientSize = {640, 480}}) == LWS::Result::Success);
        REQUIRE(child.Create({.parent = &parent, .clientSize = {200, 160}}) == LWS::Result::Success);
        REQUIRE(grandchild.Create({.parent = &child, .clientSize = {80, 60}}) == LWS::Result::Success);
        const HWND parentHwnd = *LWS::Win32::GetHwnd(parent);
        const HWND childHwnd = *LWS::Win32::GetHwnd(child);
        const HWND grandchildHwnd = *LWS::Win32::GetHwnd(grandchild);
        const UINT target = *LWS::Win32::GetDpi(parent);
        const UINT previous = increasing ? target / 2 : target * 2;
        RECT suggested{};
        REQUIRE(GetWindowRect(parentHwnd, &suggested));
        suggested.right += 40;
        // Synthetic WM_DPICHANGED seeds an old cache without requiring another monitor.
        // BEFOREPARENT must replace it using the real HWND DPI, not message parameters.
        for (HWND hwnd : {childHwnd, grandchildHwnd})
            SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(previous, previous), reinterpret_cast<LPARAM>(&suggested));
        unsigned childEvents = 0, grandchildEvents = 0, parentEvents = 0;
        auto grandchildListener = grandchild.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                    ++grandchildEvents;
                return LWS::EventResponse::Unhandled;
            });
        auto childListener = child.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                {
                    ++childEvents;
                    REQUIRE(grandchild.RequestPlacement(
                                {.position = LWS::Point{5, 7}, .clientSize = LWS::LogicalSize{100, 70}}) ==
                            LWS::Result::Success);
                }
                return LWS::EventResponse::Unhandled;
            });
        auto parentListener = parent.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                {
                    ++parentEvents;
                    CHECK(LWS::Win32::GetDpi(child) == target);
                    CHECK(LWS::Win32::GetDpi(grandchild) == target);
                    REQUIRE(child.RequestPlacement({.position = LWS::Point{11, 13},
                                                    .clientSize = LWS::LogicalSize{300, 180}}) == LWS::Result::Success);
                }
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(grandchildListener);
        REQUIRE(childListener);
        REQUIRE(parentListener);
        // Windows traverses BEFOREPARENT bottom-up, then AFTERPARENT top-down.
        SendMessageW(grandchildHwnd, WM_DPICHANGED_BEFOREPARENT, 0, 0);
        SendMessageW(childHwnd, WM_DPICHANGED_BEFOREPARENT, 0, 0);
        CHECK(childEvents == 0);
        CHECK(grandchildEvents == 0);
        SendMessageW(parentHwnd, WM_DPICHANGED, MAKELONG(target, target), reinterpret_cast<LPARAM>(&suggested));
        REQUIRE(parentEvents > 0);
        const auto checkBounds = [&](LWS::Window& window, HWND hwnd, LWS::Point origin, LWS::LogicalSize size)
        {
            const auto metrics = window.GetClientAreaMetrics();
            CHECK(metrics.logical == size);
            REQUIRE(metrics.pixels);
            CHECK(metrics.pixels->x == MulDiv(size.x, target, 96));
            CHECK(metrics.pixels->y == MulDiv(size.y, target, 96));
            POINT point{};
            REQUIRE(ClientToScreen(hwnd, &point));
            REQUIRE(ScreenToClient(GetParent(hwnd), &point));
            CHECK(point.x == MulDiv(origin.x, target, 96));
            CHECK(point.y == MulDiv(origin.y, target, 96));
        };
        checkBounds(child, childHwnd, {11, 13}, {300, 180});
        checkBounds(grandchild, grandchildHwnd, {5, 7}, {100, 70});
        const auto settledChild = childEvents, settledGrandchild = grandchildEvents;
        SendMessageW(childHwnd, WM_DPICHANGED_AFTERPARENT, 0, 0);
        SendMessageW(grandchildHwnd, WM_DPICHANGED_AFTERPARENT, 0, 0);
        CHECK(childEvents == settledChild);
        CHECK(grandchildEvents == settledGrandchild);
    }
}

TEST_CASE("Child DPI completion publishes unchanged physical bounds at the new scale", "[dpi][window][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window parent(platform), child(platform);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .clientSize = {200, 100}}) == LWS::Result::Success);
    const HWND hwnd = *LWS::Win32::GetHwnd(child);
    const UINT target = *LWS::Win32::GetDpi(child);
    SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(target * 2, target * 2), 0);
    const auto oldMetrics = child.GetClientAreaMetrics();
    unsigned events = 0;
    auto listener = child.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                ++events;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener);
    SendMessageW(hwnd, WM_DPICHANGED_BEFOREPARENT, 0, 0);
    CHECK(LWS::Win32::GetDpi(child) == target);
    CHECK(events == 0);
    CHECK(child.GetClientAreaMetrics() == oldMetrics);
    SendMessageW(hwnd, WM_DPICHANGED_AFTERPARENT, 0, 0);
    CHECK(events == 1);
    CHECK(child.GetClientAreaMetrics().pixels == oldMetrics.pixels);
    CHECK(child.GetClientAreaMetrics().logical == LWS::LogicalSize{200, 100});
    SendMessageW(hwnd, WM_DPICHANGED_AFTERPARENT, 0, 0);
    CHECK(events == 1);
}

TEST_CASE("Nested child layout survives real mixed-DPI monitor moves", "[.][dpi][multimonitor][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    const auto monitors = platform.GetMonitors();
    REQUIRE(monitors);
    REQUIRE_FALSE(monitors->empty());
    const auto& first = monitors->front();
    const auto second = std::ranges::find_if(*monitors, [&](const auto& monitor)
                                             { return monitor.contentScale != first.contentScale; });
    if (second == monitors->end())
        SKIP("Requires two monitors with different DPI scales");
    LWS::Window parent(platform), child(platform), grandchild(platform);
    REQUIRE(parent.Create({.clientSize = {640, 480}}) == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .clientSize = {300, 180}}) == LWS::Result::Success);
    REQUIRE(grandchild.Create({.parent = &child, .clientSize = {100, 70}}) == LWS::Result::Success);
    const auto layout = [&]
    {
        REQUIRE(child.RequestPlacement({.position = LWS::Point{11, 13}, .clientSize = LWS::LogicalSize{300, 180}}) ==
                LWS::Result::Success);
        REQUIRE(grandchild.RequestPlacement({.position = LWS::Point{5, 7}, .clientSize = LWS::LogicalSize{100, 70}}) ==
                LWS::Result::Success);
    };
    auto listener = parent.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                layout();
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener);
    for (const auto* monitor : {&first, &*second, &first})
    {
        CAPTURE(monitor->contentScale.x);
        const auto origin = monitor->workRect.GetCorner(LLUtils::TopLeft);
        REQUIRE(SetWindowPos(*LWS::Win32::GetHwnd(parent), nullptr, origin.x + 40, origin.y + 40, 800, 600,
                             SWP_NOZORDER | SWP_NOACTIVATE));
        REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
        const UINT dpi = static_cast<UINT>(std::lround(monitor->contentScale.x * 96));
        CHECK(LWS::Win32::GetDpi(parent) == dpi);
        CHECK(LWS::Win32::GetDpi(child) == dpi);
        CHECK(LWS::Win32::GetDpi(grandchild) == dpi);
        const auto childMetrics = child.GetClientAreaMetrics();
        const auto grandchildMetrics = grandchild.GetClientAreaMetrics();
        CHECK(childMetrics.logical == LWS::LogicalSize{300, 180});
        CHECK(grandchildMetrics.logical == LWS::LogicalSize{100, 70});
        REQUIRE(childMetrics.pixels);
        REQUIRE(grandchildMetrics.pixels);
        CHECK(childMetrics.pixels->x == MulDiv(300, dpi, 96));
        CHECK(childMetrics.pixels->y == MulDiv(180, dpi, 96));
        CHECK(grandchildMetrics.pixels->x == MulDiv(100, dpi, 96));
        CHECK(grandchildMetrics.pixels->y == MulDiv(70, dpi, 96));
    }
}

TEST_CASE("Unchanged immutable resources retain native objects across window transitions",
          "[cursor][icon][reuse][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window first(platform), second(platform);
    std::array<std::byte, 16 * 16 * 4> pixels{};
    const LWS::BitmapBuffer bitmap{.pixels = pixels, .width = 16, .height = 16, .rowPitch = 64};
    auto cursor = LWS::Cursor::FromBitmap(bitmap, {2, 3});
    auto icon = LWS::WindowIcon::FromBitmap(bitmap);
    REQUIRE(cursor);
    REQUIRE(icon);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(first.Create() == LWS::Result::Success);
    REQUIRE(second.Create() == LWS::Result::Success);
    const HWND hwnd = *LWS::Win32::GetHwnd(first);
    const auto bigIcon = SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    REQUIRE(bigIcon != 0);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    const HCURSOR firstCursor = GetCursor();
    REQUIRE(firstCursor != nullptr);
    REQUIRE(second.SetMouseCursor(*cursor) == LWS::Result::Success);
    const HCURSOR secondCursor = GetCursor();
    REQUIRE(secondCursor != nullptr);
    REQUIRE(secondCursor != firstCursor);
    auto workerCursor = std::async(std::launch::async, [value = *cursor] { return value; }).get();
    auto workerIcon = std::async(std::launch::async, [value = *icon] { return value; }).get();
    REQUIRE(first.SetMouseCursor(workerCursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetWindowIcon(workerIcon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
        REQUIRE(GetCursor() == firstCursor);
        REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
        REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    }
    REQUIRE(first.SetMouseCursorVisible(false) == LWS::Result::Success);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == nullptr);
    REQUIRE(second.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == secondCursor);
    REQUIRE(first.SetMouseCursorVisible(true) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetVisible(true) == LWS::Result::Success);
    REQUIRE(first.SetVisible(false) == LWS::Result::Success);
    RECT rectangle{};
    REQUIRE(GetWindowRect(hwnd, &rectangle));
    SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&rectangle));
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    REQUIRE(LWS::Win32::GetHwnd(first) == hwnd);
    // Reapplying still reinstalls native properties, even though it reuses their allocation.
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0) == bigIcon);
    auto replacementCursor = LWS::Cursor::FromBitmap(bitmap, {1, 1});
    auto replacementIcon = LWS::WindowIcon::FromBitmap(bitmap);
    REQUIRE(replacementCursor);
    REQUIRE(replacementIcon);
    REQUIRE(first.SetMouseCursor(*replacementCursor) == LWS::Result::Success);
    REQUIRE(GetCursor() != firstCursor);
    REQUIRE(first.SetWindowIcon(*replacementIcon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) != bigIcon);
    REQUIRE(first.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Arrow)) == LWS::Result::Success);
    REQUIRE(first.SetWindowIcon(std::nullopt) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == 0);
    REQUIRE(first.Destroy() == LWS::Result::Success);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::InvalidState);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::InvalidState);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

TEST_CASE("Process bootstrap policy is idempotent and immutable", "[.][bootstrap][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) ==
            LWS::Result::InvalidState);
}

TEST_CASE("Failed DPI adoption can be retried after process configuration", "[.][bootstrap][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) == LWS::Result::Failure);
    REQUIRE(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::InvalidState);
}

TEST_CASE("Process DPI contracts run in isolated processes", "[bootstrap-suite][win32]")
{
    wchar_t executable[32768]{};
    REQUIRE(GetModuleFileNameW(nullptr, executable, 32768) != 0);
    for (const wchar_t* test : {L"Process bootstrap rejects a thread-only DPI override",
                                L"Process bootstrap recognizes process awareness behind a thread override",
                                L"Process bootstrap policy is idempotent and immutable",
                                L"Failed DPI adoption can be retried after process configuration"})
    {
        std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + test + L"\" --reporter compact";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                               &startup, &process));
        const DWORD waited = WaitForSingleObject(process.hProcess, 15000);
        if (waited != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 99);
        DWORD result{};
        GetExitCodeProcess(process.hProcess, &result);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        INFO("DPI child exit code " << result);
        REQUIRE(waited == WAIT_OBJECT_0);
        REQUIRE(result == 0);
    }
}
TEST_CASE("Window style replacement updates the native frame once", "[win32][styles]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    const auto hwnd = *LWS::Win32::GetHwnd(window);
    // NoStyle must be honored at creation, including when setting it again is a no-op.
    const auto requireBorderless = [&]
    {
        REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
        RECT client{}, outer{};
        REQUIRE(GetClientRect(hwnd, &client));
        REQUIRE(GetWindowRect(hwnd, &outer));
        POINT origin{};
        REQUIRE(ClientToScreen(hwnd, &origin));
        REQUIRE(origin.x == outer.left);
        REQUIRE(origin.y == outer.top);
        REQUIRE(client.right == outer.right - outer.left);
        REQUIRE(client.bottom == outer.bottom - outer.top);
    };
    requireBorderless();
    REQUIRE(window.SetWindowStyles(LWS::WindowStyle::NoStyle) == LWS::Result::Success);
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    requireBorderless();
    unsigned updates{};
    const SUBCLASSPROC observe = [](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                    DWORD_PTR data) -> LRESULT
    {
        if (message == WM_STYLECHANGED && static_cast<int>(wparam) == GWL_STYLE)
            ++*reinterpret_cast<unsigned*>(data);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    };
    REQUIRE(SetWindowSubclass(hwnd, observe, 1, reinterpret_cast<DWORD_PTR>(&updates)));
    const LWS::WindowStyleFlags styles(LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton);
    REQUIRE(window.SetWindowStyles(styles) == LWS::Result::Success);
    REQUIRE(window.GetWindowStyles() == styles);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
    REQUIRE(updates == 1);
    REQUIRE(window.SetWindowStyles(styles) == LWS::Result::Success);
    REQUIRE(updates == 1);
    REQUIRE(window.SetWindowStyles(LWS::WindowStyleFlags(LWS::WindowStyle::NoStyle)) == LWS::Result::Success);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
    REQUIRE(updates == 2);
    requireBorderless();
    REQUIRE(RemoveWindowSubclass(hwnd, observe, 1));
}

TEST_CASE("Captionless resize frames have no top strip and retain edge resizing", "[win32][styles][frame]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(context);
    REQUIRE(window.Create({.clientSize = {640, 480}, .styles = LWS::WindowStyle::ResizableBorder}) ==
            LWS::Result::Success);
    const HWND hwnd = *LWS::Win32::GetHwnd(window);
    const auto checkFrame = [&]
    {
        RECT outer{};
        REQUIRE(GetWindowRect(hwnd, &outer));
        POINT origin{};
        REQUIRE(ClientToScreen(hwnd, &origin));
        CHECK(origin.y == outer.top);
        CHECK(origin.x > outer.left);
        CHECK(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM((outer.left + outer.right) / 2, outer.top + 1)) == HTTOP);
        CHECK(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(outer.left + 1, outer.top + 1)) == HTTOPLEFT);
        CHECK(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(outer.right - 2, outer.top + 1)) == HTTOPRIGHT);
    };
    CHECK(window.GetClientAreaMetrics().logical == LWS::LogicalSize{640, 480});
    checkFrame();
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    checkFrame();
    LWS::Window child(context);
    REQUIRE(
        child.Create({.parent = &window, .position = LWS::Point{0, 0}, .clientSize = {640, 480}, .visible = true}) ==
        LWS::Result::Success);
    POINT top{};
    REQUIRE(ClientToScreen(hwnd, &top));
    const HWND childHwnd = *LWS::Win32::GetHwnd(child);
    CHECK(SendMessageW(childHwnd, WM_NCHITTEST, 0, MAKELPARAM(top.x + 100, top.y + 1)) == HTTRANSPARENT);
    CHECK(SendMessageW(childHwnd, WM_NCHITTEST, 0, MAKELPARAM(top.x + 100, top.y + 100)) == HTCLIENT);

    REQUIRE(window.SetWindowStyles(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder) ==
            LWS::Result::Success);
    RECT outer{};
    REQUIRE(GetWindowRect(hwnd, &outer));
    POINT origin{};
    REQUIRE(ClientToScreen(hwnd, &origin));
    CHECK(origin.y > outer.top);
    REQUIRE(window.SetWindowStyles(LWS::WindowStyle::ResizableBorder) == LWS::Result::Success);
    checkFrame();
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{800, 600}}) == LWS::Result::Success);
    CHECK(window.GetClientAreaMetrics().logical == LWS::LogicalSize{800, 600});
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Continue);
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor));
    origin = {};
    REQUIRE(ClientToScreen(hwnd, &origin));
    CHECK(origin.y == monitor.rcWork.top);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    checkFrame();
}

TEST_CASE("Hidden resize borders retain sizing and child edge input", "[win32][styles][frame]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(context);
    CHECK(LWS::Win32::SetResizeBorderVisible(window, false) == LWS::Result::InvalidState);
    REQUIRE(window.Create({.clientSize = {640, 480}, .styles = LWS::WindowStyle::ResizableBorder, .visible = true}) ==
            LWS::Result::Success);
    const HWND hwnd = *LWS::Win32::GetHwnd(window);
    REQUIRE(LWS::Win32::SetResizeBorderVisible(window, false) == LWS::Result::Success);
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{640, 480}}) == LWS::Result::Success);
    CHECK(window.GetClientAreaMetrics().logical == LWS::LogicalSize{640, 480});
    LWS::Window child(context);
    REQUIRE(
        child.Create({.parent = &window, .position = LWS::Point{0, 0}, .clientSize = {640, 480}, .visible = true}) ==
        LWS::Result::Success);
    CHECK(LWS::Win32::SetResizeBorderVisible(child, false) == LWS::Result::NotSupported);
    const HWND childHwnd = *LWS::Win32::GetHwnd(child);
    const auto checkFrame = [&]
    {
        RECT outer{}, client{};
        POINT origin{};
        REQUIRE(GetWindowRect(hwnd, &outer));
        REQUIRE(GetClientRect(hwnd, &client));
        REQUIRE(ClientToScreen(hwnd, &origin));
        CHECK(origin.x == outer.left);
        CHECK(origin.y == outer.top);
        CHECK(client.right == outer.right - outer.left);
        CHECK(client.bottom == outer.bottom - outer.top);
        const LONG centerX = (outer.left + outer.right) / 2, centerY = (outer.top + outer.bottom) / 2;
        const std::pair<POINT, LRESULT> edges[]{{{outer.left + 1, centerY}, HTLEFT},
                                                {{outer.right - 2, centerY}, HTRIGHT},
                                                {{centerX, outer.top + 1}, HTTOP},
                                                {{centerX, outer.bottom - 2}, HTBOTTOM},
                                                {{outer.left + 1, outer.top + 1}, HTTOPLEFT},
                                                {{outer.right - 2, outer.top + 1}, HTTOPRIGHT},
                                                {{outer.left + 1, outer.bottom - 2}, HTBOTTOMLEFT},
                                                {{outer.right - 2, outer.bottom - 2}, HTBOTTOMRIGHT}};
        for (const auto& [point, hit] : edges)
        {
            CHECK(SendMessageW(hwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == hit);
            CHECK(SendMessageW(childHwnd, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y)) == HTTRANSPARENT);
        }
        CHECK(SendMessageW(childHwnd, WM_NCHITTEST, 0, MAKELPARAM(centerX, centerY)) == HTCLIENT);
    };
    checkFrame();
    REQUIRE(window.SetClientSizeLimits({.minimum = {320, 240}, .maximum = {900, 700}}) == LWS::Result::Success);
    MINMAXINFO limits{};
    SendMessageW(hwnd, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    const double scale = *LWS::Win32::GetDpi(window) / 96.0;
    CHECK(limits.ptMinTrackSize.x == std::lround(320 * scale));
    CHECK(limits.ptMinTrackSize.y == std::lround(240 * scale));
    CHECK(limits.ptMaxTrackSize.x == std::lround(900 * scale));
    CHECK(limits.ptMaxTrackSize.y == std::lround(700 * scale));
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    MONITORINFO monitor{sizeof(monitor)};
    REQUIRE(GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor));
    POINT maximizedOrigin{};
    REQUIRE(ClientToScreen(hwnd, &maximizedOrigin));
    CHECK(maximizedOrigin.x >= monitor.rcWork.left);
    CHECK(maximizedOrigin.y >= monitor.rcWork.top);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    checkFrame();
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::Success);
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Windowed) == LWS::Result::Success);
    checkFrame();
    REQUIRE(LWS::Win32::SetResizeBorderVisible(window, true) == LWS::Result::Success);
    RECT outer{};
    POINT origin{};
    REQUIRE(GetWindowRect(hwnd, &outer));
    REQUIRE(ClientToScreen(hwnd, &origin));
    CHECK(origin.x > outer.left);
    CHECK((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_THICKFRAME) != 0);
}
#endif
