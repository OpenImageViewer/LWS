#ifdef LWS_PLATFORM_WIN32

    #include <catch2/catch_test_macros.hpp>
    #include <catch2/generators/catch_generators.hpp>

    #include <LWS/Clipboard.hpp>
    #include <LWS/Cursor.hpp>
    #include <LWS/FileDialog.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/source/Win32/internal/WindowPosHelper.hpp>

    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <Ole2.h>

    #include <array>
    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <cmath>
    #include <thread>
    #include <type_traits>
    #include <vector>

namespace
{
    BOOL CALLBACK CollectMonitors(HMONITOR monitor, HDC, LPRECT, LPARAM data)
    {
        reinterpret_cast<std::vector<HMONITOR>*>(data)->push_back(monitor);
        return TRUE;
    }

    class Context final
    {
      public:

        Context()
        {
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(value.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
        }

        LWS::PlatformContext value;
    };

    HWND Hwnd(LWS::Window& window)
    {
        const auto handle = LWS::Win32::GetHwnd(window);
        REQUIRE(handle.has_value());
        return *handle;
    }

    std::expected<LWS::WindowIcon, LWS::Result> MakeWindowIcon(std::byte blue)
    {
        const std::array pixels{blue, std::byte{0}, std::byte{0}, std::byte{255}};
        return LWS::WindowIcon::FromBitmap({
            .pixels = pixels,
            .width = 1,
            .height = 1,
            .rowPitch = 4,
        });
    }

    HICON WindowIconHandle(HWND window, WPARAM size)
    {
        return reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, size, 0));
    }

    void Pump(LWS::PlatformContext& context)
    {
        std::ignore = context.ProcessMessages();
    }
}  // namespace

TEST_CASE("Platform context is one-shot", "[platform][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    REQUIRE(context.IsUsable());
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::InvalidState);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE_FALSE(context.IsUsable());
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::InvalidState);
}

TEST_CASE("Platform rejects invalid and unavailable backends", "[platform][win32]")
{
    LWS::PlatformContext context;
    REQUIRE(context.Init({}) == LWS::Result::InvalidArgument);
    REQUIRE(context.Init({.backend = LWS::BackendId::X11}) == LWS::Result::NotSupported);
}

TEST_CASE("Independent contexts run on independent UI threads", "[platform][thread][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    std::atomic_uint successes{};
    auto run = [&]
    {
        LWS::PlatformContext context;
        if (context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success)
        {
            ++successes;
            std::ignore = context.Shutdown();
        }
    };
    std::jthread first(run);
    std::jthread second(run);
    first.join();
    second.join();
    REQUIRE(successes == 2);
}

TEST_CASE("MTA context keeps ordinary windowing available", "[platform][com][win32]")
{
    std::jthread thread(
        []
        {
            REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
            {
                Context context;
                LWS::Window window(context.value);
                REQUIRE(window.Create() == LWS::Result::Success);
                REQUIRE(window.SetDragAndDropEnabled(true) != LWS::Result::Success);
            }
            CoUninitialize();
        });
}

TEST_CASE("Window is stable, final, and one-shot", "[window][win32]")
{
    STATIC_REQUIRE(std::is_final_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<LWS::Window>);

    Context context;
    LWS::Window window(context.value);
    REQUIRE_FALSE(window.IsCreated());
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::InvalidState);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::InvalidState);
}

TEST_CASE("Context shutdown rejects a bound C++ window", "[platform][window][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    {
        LWS::Window window(context);
        REQUIRE(context.Shutdown() == LWS::Result::InvalidState);
    }
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Window basic properties round-trip", "[window][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.title = L"initial", .position = LWS::Point{80, 90}, .clientSize = {640, 480}}) ==
            LWS::Result::Success);

    REQUIRE(window.SetTitle(L"updated") == LWS::Result::Success);
    REQUIRE(window.GetTitle() == L"updated");
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    REQUIRE(window.IsVisible());
    REQUIRE(window.RequestPlacement({.position = LWS::Point{120, 140}}) == LWS::Result::Success);
    REQUIRE(window.GetPlacement().position.has_value());
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{720, 520}}) == LWS::Result::Success);
    REQUIRE(window.GetClientAreaMetrics().logical == LWS::LogicalSize{720, 520});
    REQUIRE(window.SetAlwaysOnTop(true) == LWS::Result::Success);
    REQUIRE(window.IsAlwaysOnTop());
    REQUIRE(window.SetTransparent(true) == LWS::Result::Success);
    REQUIRE(window.IsTransparent());
}

TEST_CASE("Window icons apply before creation, replace, and reset", "[window][icon][win32]")
{
    Context context;
    LWS::Window window(context.value);
    const auto firstIcon = MakeWindowIcon(std::byte{255});
    REQUIRE(firstIcon.has_value());
    REQUIRE(window.SetWindowIcon(*firstIcon) == LWS::Result::Success);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);

    const HWND handle = Hwnd(window);
    const HICON initialBig = WindowIconHandle(handle, ICON_BIG);
    const HICON initialSmall = WindowIconHandle(handle, ICON_SMALL);
    REQUIRE(initialBig != nullptr);
    REQUIRE(initialSmall == initialBig);

    const auto replacement = MakeWindowIcon(std::byte{127});
    REQUIRE(replacement.has_value());
    REQUIRE(window.SetWindowIcon(*replacement) == LWS::Result::Success);
    const HICON replacementBig = WindowIconHandle(handle, ICON_BIG);
    REQUIRE(replacementBig != nullptr);
    REQUIRE(replacementBig != initialBig);
    REQUIRE(WindowIconHandle(handle, ICON_SMALL) == replacementBig);

    REQUIRE(window.SetWindowIcon(std::nullopt) == LWS::Result::Success);
    REQUIRE(WindowIconHandle(handle, ICON_BIG) == nullptr);
    REQUIRE(WindowIconHandle(handle, ICON_SMALL) == nullptr);
}

TEST_CASE("Failed creation preserves a window icon for retry", "[window][icon][win32]")
{
    bool iconAppliedAfterRetry{};
    std::jthread thread(
        [&]
        {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(comResult))
                return;

            {
                LWS::PlatformContext platform;
                if (LWS::Win32::BootstrapProcess() == LWS::Result::Success &&
                    platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success)
                {
                    LWS::Window window(platform);
                    const auto icon = MakeWindowIcon(std::byte{255});
                    if (icon.has_value() && window.SetWindowIcon(*icon) == LWS::Result::Success &&
                        window.Create({.dragAndDropEnabled = true}) == LWS::Result::NotSupported &&
                        window.Create() == LWS::Result::Success)
                    {
                        const HWND handle = *LWS::Win32::GetHwnd(window);
                        const HICON big = WindowIconHandle(handle, ICON_BIG);
                        iconAppliedAfterRetry = big != nullptr && WindowIconHandle(handle, ICON_SMALL) == big;
                    }
                }
            }
            CoUninitialize();
        });
    thread.join();

    REQUIRE(iconAppliedAfterRetry);
}

TEST_CASE("Window min and max client sizes round-trip", "[window][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetClientSizeLimits({{100, 120}, {900, 700}}) == LWS::Result::Success);
    REQUIRE(window.GetClientSizeLimits().minimum == LWS::LogicalSize{100, 120});
    REQUIRE(window.GetClientSizeLimits().maximum == LWS::LogicalSize{900, 700});
}

TEST_CASE("Placement preserves show-state requests made by resize listeners", "[window][geometry][win32]")
{
    const bool combined = GENERATE(false, true);
    Context context;
    LWS::Window window(context.value);
    REQUIRE(
        window.Create({.clientSize = {400, 300},
                       .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                                       LWS::WindowStyle::MaximizeButton),
                       .visible = true}) == LWS::Result::Success);
    bool requested = false;
    LWS::Result result = LWS::Result::Failure;
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (!requested && std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
            {
                requested = true;
                result = window.RequestShowState(LWS::WindowShowState::Maximized);
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    REQUIRE(window.RequestPlacement({.position = combined ? std::optional(LWS::Point{120, 140}) : std::nullopt,
                                     .clientSize = LWS::LogicalSize{500, 350}}) == LWS::Result::Success);
    REQUIRE(requested);
    REQUIRE(result == LWS::Result::Success);
    REQUIRE(window.GetShowState() == LWS::WindowShowState::Maximized);
    REQUIRE(IsZoomed(Hwnd(window)));
}

TEST_CASE("Placement retains ordinary restored and maximized show states", "[window][geometry][win32]")
{
    const auto state = GENERATE(LWS::WindowShowState::Restored, LWS::WindowShowState::Maximized);
    Context context;
    LWS::Window window(context.value);
    REQUIRE(
        window.Create({.clientSize = {400, 300},
                       .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                                       LWS::WindowStyle::MaximizeButton),
                       .visible = true}) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(state) == LWS::Result::Success);
    REQUIRE(window.RequestPlacement({.position = LWS::Point{120, 140}, .clientSize = LWS::LogicalSize{500, 350}}) ==
            LWS::Result::Success);
    REQUIRE(window.GetShowState() == state);
    REQUIRE((IsZoomed(Hwnd(window)) != FALSE) == (state == LWS::WindowShowState::Maximized));
}

TEST_CASE("Show-state listeners observe the last published client metrics", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(
        window.Create({.clientSize = {400, 300},
                       .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                                       LWS::WindowStyle::MaximizeButton),
                       .visible = true}) == LWS::Result::Success);
    const auto previous = window.GetClientAreaMetrics();
    bool stateObserved = false;
    bool metricsObserved = false;
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* state = std::get_if<LWS::EventShowStateChanged>(&event))
            {
                REQUIRE(state->state == LWS::WindowShowState::Maximized);
                REQUIRE(window.GetClientAreaMetrics() == previous);
                REQUIRE_FALSE(metricsObserved);
                stateObserved = true;
            }
            if (const auto* metrics = std::get_if<LWS::EventClientAreaSizeChanged>(&event))
            {
                REQUIRE(stateObserved);
                REQUIRE(window.GetClientAreaMetrics() == metrics->size);
                metricsObserved = true;
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(stateObserved);
    REQUIRE(metricsObserved);
    REQUIRE(window.GetClientAreaMetrics() != previous);
}

TEST_CASE("Nested show-state changes retain final native metrics", "[window][metrics][win32]")
{
    enum class Action
    {
        Restore,
        Resize,
        Minimize,
        Destroy,
    };
    const auto action = GENERATE(Action::Restore, Action::Resize, Action::Minimize, Action::Destroy);
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.clientSize = {400, 300},
                           .styles = LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                     LWS::WindowStyle::MaximizeButton,
                           .visible = true}) == LWS::Result::Success);
    bool changed = false;
    LWS::Result nested = LWS::Result::Failure;
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* state = std::get_if<LWS::EventShowStateChanged>(&event);
                state != nullptr && state->state == LWS::WindowShowState::Maximized && !changed)
            {
                changed = true;
                switch (action)
                {
                    case Action::Restore:
                        nested = window.RequestShowState(LWS::WindowShowState::Restored);
                        break;
                    case Action::Resize:
                        nested = window.RequestPlacement({.clientSize = LWS::LogicalSize{500, 350}});
                        break;
                    case Action::Minimize:
                        nested = window.RequestShowState(LWS::WindowShowState::Minimized);
                        break;
                    case Action::Destroy:
                        nested = window.Destroy();
                        break;
                }
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(changed);
    REQUIRE(nested == LWS::Result::Success);
    const auto metrics = window.GetClientAreaMetrics();
    if (action == Action::Destroy)
    {
        REQUIRE_FALSE(window.IsCreated());
        REQUIRE_FALSE(metrics.pixels.has_value());
    }
    else if (action == Action::Minimize)
    {
        REQUIRE(window.GetShowState() == LWS::WindowShowState::Minimized);
        REQUIRE(metrics.logical == LWS::LogicalSize{0, 0});
        REQUIRE(metrics.pixels == LWS::PixelSize{0, 0});
    }
    else
    {
        const HWND handle = Hwnd(window);
        RECT client{};
        REQUIRE(GetClientRect(handle, &client));
        REQUIRE(metrics.pixels == LWS::PixelSize{client.right - client.left, client.bottom - client.top});
        const auto dpi = static_cast<int>(GetDpiForWindow(handle));
        REQUIRE(dpi > 0);
        REQUIRE(metrics.logical == LWS::LogicalSize{MulDiv(client.right - client.left, 96, dpi),
                                                    MulDiv(client.bottom - client.top, 96, dpi)});
        REQUIRE(window.GetPlacement().clientSize == metrics.logical);
    }
}

TEST_CASE("Workspace offsets preserve top and left reservations and tool windows", "[window][geometry][win32]")
{
    const bool toolWindow = GENERATE(false, true);
    const LONG left = GENERATE(0, 40);
    const LONG top = GENERATE(0, 30);
    const MONITORINFO monitor{
        .cbSize = sizeof(MONITORINFO),
        .rcMonitor = {-1920, -1080, 0, 0},
        .rcWork = {-1920 + left, -1080 + top, 0, 0},
    };
    const auto offset = LWS::internal::WindowPosHelper::workspaceOffset(monitor, toolWindow ? WS_EX_TOOLWINDOW : 0);
    REQUIRE(offset.x == (toolWindow ? 0 : left));
    REQUIRE(offset.y == (toolWindow ? 0 : top));
}

TEST_CASE("Top-level placement positions use screen coordinates across show states", "[window][geometry][win32]")
{
    const bool toolWindow = GENERATE(false, true);
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.clientSize = {400, 300},
                           .styles = LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                     LWS::WindowStyle::MaximizeButton,
                           .visible = true}) == LWS::Result::Success);
    const HWND handle = Hwnd(window);
    if (toolWindow)
        SetWindowLongPtrW(handle, GWL_EXSTYLE, GetWindowLongPtrW(handle, GWL_EXSTYLE) | WS_EX_TOOLWINDOW);
    REQUIRE(window.RequestPlacement({.position = LWS::Point{120, 140}, .clientSize = LWS::LogicalSize{400, 300}}) ==
            LWS::Result::Success);
    RECT screen{};
    REQUIRE(GetWindowRect(handle, &screen));
    const int dpi = static_cast<int>(GetDpiForWindow(handle));
    REQUIRE(dpi > 0);
    const LWS::Point position{MulDiv(screen.left, 96, dpi), MulDiv(screen.top, 96, dpi)};
    REQUIRE(window.GetPlacement().position == position);
    unsigned moves = 0;
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* moved = std::get_if<LWS::EventMove>(&event))
            {
                ++moves;
                CHECK(moved->newPosition == position);
                CHECK(window.GetPlacement().position == position);
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    for (const auto state :
         {LWS::WindowShowState::Maximized, LWS::WindowShowState::Minimized, LWS::WindowShowState::Restored,
          LWS::WindowShowState::Minimized, LWS::WindowShowState::Restored, LWS::WindowShowState::Maximized,
          LWS::WindowShowState::Restored})
    {
        REQUIRE(window.RequestShowState(state) == LWS::Result::Success);
        REQUIRE(window.GetPlacement().position == position);
    }
    REQUIRE(moves > 0);
    REQUIRE(window.RequestPlacement({.position = window.GetPlacement().position}) == LWS::Result::Success);
    RECT roundtrip{};
    REQUIRE(GetWindowRect(handle, &roundtrip));
    REQUIRE(roundtrip.left == screen.left);
    REQUIRE(roundtrip.top == screen.top);
}

TEST_CASE("Window metrics are coherent", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE_FALSE(window.GetClientAreaMetrics().pixels.has_value());
    REQUIRE(window.Create({.clientSize = {400, 300}}) == LWS::Result::Success);
    const auto size = window.GetClientAreaMetrics();
    REQUIRE(size.pixels.has_value());
    REQUIRE(size.logical == LWS::LogicalSize{400, 300});
    RECT client{};
    REQUIRE(GetClientRect(Hwnd(window), &client));
    REQUIRE(size.pixels == LWS::PixelSize{client.right - client.left, client.bottom - client.top});
    REQUIRE(size.Scale().has_value());
    REQUIRE(size.Scale()->x > 0.0);
    bool cleanup = false;
    auto listener = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
            {
                cleanup = true;
                REQUIRE_FALSE(window.IsConfigured());
                REQUIRE(window.GetClientAreaMetrics() == size);
            }
            if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                REQUIRE_FALSE(window.GetClientAreaMetrics().pixels.has_value());
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(listener.has_value());
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(cleanup);
    REQUIRE(window.GetClientAreaMetrics().logical == size.logical);
    REQUIRE_FALSE(window.GetClientAreaMetrics().pixels.has_value());
}

TEST_CASE("Maximized show state always requests windowed maximization", "[window][maximize][win32]")
{
    const auto mode = GENERATE(LWS::WindowMode::Windowed, LWS::WindowMode::Fullscreen,
                               LWS::WindowMode::FullscreenAllMonitors);
    const auto state = GENERATE(LWS::WindowShowState::Restored, LWS::WindowShowState::Minimized,
                                LWS::WindowShowState::Maximized);
    Context context;
    LWS::Window window(context.value);
    REQUIRE(
        window.Create({.clientSize = {400, 300},
                       .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder |
                                                       LWS::WindowStyle::MaximizeButton),
                       .visible = true}) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(state) == LWS::Result::Success);
    REQUIRE(window.SetWindowMode(mode) == LWS::Result::Success);
    const HMONITOR monitor = MonitorFromWindow(Hwnd(window), MONITOR_DEFAULTTONEAREST);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(window.GetWindowMode() == LWS::WindowMode::Windowed);
    REQUIRE(window.GetShowState() == LWS::WindowShowState::Maximized);
    REQUIRE(IsZoomed(Hwnd(window)));
    REQUIRE(MonitorFromWindow(Hwnd(window), MONITOR_DEFAULTTONEAREST) == monitor);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    REQUIRE(window.GetClientAreaMetrics().logical == LWS::LogicalSize{400, 300});
}

TEST_CASE("Fullscreen restoration preserves placement after moving between DPI scales",
          "[.][window][fullscreen][win32]")
{
    const bool maximizeOnCurrentMonitor = GENERATE(false, true);
    Context context;
    std::vector<HMONITOR> monitors;
    REQUIRE(EnumDisplayMonitors(nullptr, nullptr, CollectMonitors, reinterpret_cast<LPARAM>(&monitors)));
    std::array<HMONITOR, 2> pair{};
    double firstScale = 0.0;
    for (HMONITOR monitor : monitors)
    {
        const auto info = context.value.GetMonitorInfo(reinterpret_cast<uintptr_t>(monitor));
        REQUIRE(info.has_value());
        if (pair[0] == nullptr)
        {
            pair[0] = monitor;
            firstScale = info->contentScale.x;
        }
        else if (info->contentScale.x != firstScale)
        {
            pair[1] = monitor;
            break;
        }
    }
    if (pair[1] == nullptr)
        SKIP("Requires two monitors with different DPI scales");

    for (bool reverse : {false, true})
    {
        for (bool maximized : {false, true})
        {
            CAPTURE(reverse, maximized, maximizeOnCurrentMonitor);
            const HMONITOR source = pair[reverse ? 1 : 0];
            const HMONITOR target = pair[reverse ? 0 : 1];
            MONITORINFO sourceInfo{sizeof(MONITORINFO)};
            MONITORINFO targetInfo{sizeof(MONITORINFO)};
            REQUIRE(GetMonitorInfoW(source, &sourceInfo));
            REQUIRE(GetMonitorInfoW(target, &targetInfo));
            LWS::Window window(context.value);
            REQUIRE(window.Create({
                        .clientSize = {640, 480},
                        .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton |
                                                        LWS::WindowStyle::ResizableBorder |
                                                        LWS::WindowStyle::MaximizeButton),
                        .visible = true,
                    }) == LWS::Result::Success);
            const HWND handle = Hwnd(window);
            REQUIRE(SetWindowPos(handle, nullptr, sourceInfo.rcWork.left + 40, sourceInfo.rcWork.top + 40, 800, 600,
                                 SWP_NOZORDER | SWP_NOACTIVATE));
            Pump(context.value);
            const auto originalSize = window.GetClientAreaMetrics().logical;
            RECT originalRect{};
            REQUIRE(GetWindowRect(handle, &originalRect));
            if (maximized)
                REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
            REQUIRE(window.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::Success);
            REQUIRE(SetWindowPos(handle, nullptr, targetInfo.rcMonitor.left, targetInfo.rcMonitor.top,
                                 targetInfo.rcMonitor.right - targetInfo.rcMonitor.left,
                                 targetInfo.rcMonitor.bottom - targetInfo.rcMonitor.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE));
            Pump(context.value);
            REQUIRE(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST) == target);
            bool visitedOtherMonitor = false;
            auto connection = window.Listen(
                [&](const LWS::AnyEvent&)
                {
                    if (maximizeOnCurrentMonitor)
                        visitedOtherMonitor |= MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST) != target;
                    return LWS::EventResponse::Unhandled;
                });
            REQUIRE(connection.has_value());
            if (maximizeOnCurrentMonitor)
            {
                REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
                REQUIRE(window.GetWindowMode() == LWS::WindowMode::Windowed);
                REQUIRE(window.GetShowState() == LWS::WindowShowState::Maximized);
                REQUIRE_FALSE(visitedOtherMonitor);
            }
            else
            {
                REQUIRE(window.SetWindowMode(LWS::WindowMode::Windowed) == LWS::Result::Success);
                REQUIRE(window.GetShowState() ==
                        (maximized ? LWS::WindowShowState::Maximized : LWS::WindowShowState::Restored));
            }
            REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
            REQUIRE(MonitorFromWindow(handle, MONITOR_DEFAULTTONEAREST) ==
                    (maximizeOnCurrentMonitor ? target : source));
            const auto restoredSize = window.GetClientAreaMetrics().logical;
            CAPTURE(originalSize.x, originalSize.y, restoredSize.x, restoredSize.y);
            REQUIRE(restoredSize == originalSize);
            RECT restoredRect{};
            REQUIRE(GetWindowRect(handle, &restoredRect));
            if (maximizeOnCurrentMonitor)
            {
                REQUIRE(restoredRect.left >= targetInfo.rcWork.left);
                REQUIRE(restoredRect.top >= targetInfo.rcWork.top);
                REQUIRE(restoredRect.right <= targetInfo.rcWork.right);
                REQUIRE(restoredRect.bottom <= targetInfo.rcWork.bottom);
            }
            else
            {
                REQUIRE(restoredRect.left == originalRect.left);
                REQUIRE(restoredRect.top == originalRect.top);
                REQUIRE(restoredRect.right == originalRect.right);
                REQUIRE(restoredRect.bottom == originalRect.bottom);
            }
        }
    }
}

TEST_CASE("Repeated window mode requests preserve the current placement", "[window][state][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::InvalidState);
    REQUIRE(window.Create({.clientSize = {400, 300}, .visible = true}) == LWS::Result::Success);
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::Success);
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Windowed) == LWS::Result::Success);
    REQUIRE(window.RequestPlacement({.position = LWS::Point{120, 80}, .clientSize = LWS::LogicalSize{500, 350}}) ==
            LWS::Result::Success);
    const auto placement = window.GetPlacement();
    REQUIRE(window.SetWindowMode(LWS::WindowMode::Windowed) == LWS::Result::Success);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    REQUIRE(window.GetPlacement().position == placement.position);
    REQUIRE(window.GetClientAreaMetrics().logical == placement.clientSize);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    RECT maximized{};
    REQUIRE(GetWindowRect(Hwnd(window), &maximized));
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::Success);
    RECT repeated{};
    REQUIRE(GetWindowRect(Hwnd(window), &repeated));
    REQUIRE(repeated.left == maximized.left);
    REQUIRE(repeated.top == maximized.top);
    REQUIRE(repeated.right == maximized.right);
    REQUIRE(repeated.bottom == maximized.bottom);
    REQUIRE(window.RequestShowState(LWS::WindowShowState::Restored) == LWS::Result::Success);
    REQUIRE(window.GetClientAreaMetrics().logical == placement.clientSize);
    LWS::Window child(context.value);
    REQUIRE(child.Create({.parent = &window}) == LWS::Result::Success);
    REQUIRE(child.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::NotSupported);
}

TEST_CASE("Client area events suppress duplicate native sizes", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    unsigned changes{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            changes += std::holds_alternative<LWS::EventClientAreaSizeChanged>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create({.clientSize = {400, 300}}) == LWS::Result::Success);
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{400, 300}}) == LWS::Result::Success);
    const unsigned confirmedChanges = changes;
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{400, 300}}) == LWS::Result::Success);
    REQUIRE(changes == confirmedChanges);
    REQUIRE(window.RequestPlacement({.clientSize = LWS::LogicalSize{500, 350}}) == LWS::Result::Success);
    REQUIRE(changes == confirmedChanges + 1);
    REQUIRE(window.GetClientAreaMetrics().logical == LWS::LogicalSize{500, 350});
}

TEST_CASE("Child placement preserves logical client geometry", "[window][geometry][parent][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    LWS::Window child(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .position = LWS::Point{100, 80}, .clientSize = {200, 300}}) ==
            LWS::Result::Success);
    REQUIRE(child.GetPlacement().position == LWS::Point{100, 80});

    const HWND handle = Hwnd(child);
    SetWindowLongPtrW(handle, GWL_STYLE, GetWindowLongPtrW(handle, GWL_STYLE) | WS_VSCROLL);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(child.RequestPlacement({.position = LWS::Point{300, 120}, .clientSize = LWS::LogicalSize{200, 300}}) ==
            LWS::Result::Success);
    REQUIRE(child.GetPlacement().position == LWS::Point{300, 120});
    REQUIRE(child.GetClientAreaMetrics().logical == LWS::LogicalSize{200, 300});
    RECT client{};
    REQUIRE(GetClientRect(handle, &client));
    REQUIRE(child.GetClientAreaMetrics().pixels ==
            LWS::PixelSize{client.right - client.left, client.bottom - client.top});
}

TEST_CASE("Parent configuration creates a child relationship", "[window][parent][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    LWS::Window child(context.value);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .transparent = true}) == LWS::Result::Success);
    REQUIRE(child.GetParent() == &parent);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE_FALSE(child.IsCreated());
}

TEST_CASE("Portable event connections own listener registration", "[window][event][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(LWS::Win32::SetMouseCapture(window, true) == LWS::Result::InvalidState);
    REQUIRE(LWS::Win32::SetMouseCapture(window, false) == LWS::Result::InvalidState);
    unsigned paints{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            paints += std::holds_alternative<LWS::EventPaint>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(connection->IsConnected());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(paints > 0);
    std::string text;
    bool leave = false, captureLost = false;
    auto input = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (auto* value = std::get_if<LWS::EventTextInput>(&event))
                text += value->text;
            leave |= std::holds_alternative<LWS::EventMouseLeave>(event);
            captureLost |= std::holds_alternative<LWS::EventMouseCaptureLost>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(input);
    SendMessageW(Hwnd(window), WM_CHAR, 0xe9, 1);
    SendMessageW(Hwnd(window), WM_CHAR, 0xd83d, 1);
    SendMessageW(Hwnd(window), WM_CHAR, 0xde00, 1);
    SendMessageW(Hwnd(window), WM_CHAR, L'a', 3);
    REQUIRE(text == "é😀aaa");
    SendMessageW(Hwnd(window), WM_CHAR, 0xd83d, 1);
    SendMessageW(Hwnd(window), WM_KILLFOCUS, 0, 0);
    SendMessageW(Hwnd(window), WM_CHAR, 0xde00, 1);
    REQUIRE(text == "é😀aaa");
    SendMessageW(Hwnd(window), WM_MOUSELEAVE, 0, 0);
    REQUIRE(leave);
    REQUIRE(LWS::Win32::SetMouseCapture(window, true) == LWS::Result::Success);
    REQUIRE(GetCapture() == Hwnd(window));
    REQUIRE(LWS::Win32::SetMouseCapture(window, false) == LWS::Result::Success);
    REQUIRE(captureLost);
    const auto extent = *window.GetClientAreaMetrics().pixels;
    const std::vector<std::byte> pixels(size_t(extent.x) * extent.y * 4, std::byte{255});
    REQUIRE(window.PresentBitmap({pixels, LWS::BitmapPixelFormat::Bgra8Premultiplied, LWS::BitmapRowOrder::TopDown,
                                  uint32_t(extent.x), uint32_t(extent.y), uint32_t(extent.x * 4)}) ==
            LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(window.PresentBitmap({}) != LWS::Result::Success);
    LWS::Window other(context.value);
    REQUIRE(other.Create() == LWS::Result::Success);
    REQUIRE(LWS::Win32::SetMouseCapture(window, true) == LWS::Result::Success);
    REQUIRE(LWS::Win32::SetMouseCapture(other, false) == LWS::Result::Success);
    REQUIRE(GetCapture() == Hwnd(window));
    REQUIRE(LWS::Win32::SetMouseCapture(other, true) == LWS::Result::Success);
    REQUIRE(GetCapture() == Hwnd(other));
    REQUIRE(LWS::Win32::SetMouseCapture(window, false) == LWS::Result::Success);
    REQUIRE(GetCapture() == Hwnd(other));
    REQUIRE(LWS::Win32::SetMouseCapture(other, false) == LWS::Result::Success);
    REQUIRE(GetCapture() == nullptr);

    LWS::Result duringDestroy = LWS::Result::Success;
    auto cleanup = other.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                duringDestroy = LWS::Win32::SetMouseCapture(other, true);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(cleanup);
    auto destroyTarget = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventMouseCaptureLost>(event))
                std::ignore = other.Destroy();
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(destroyTarget);
    REQUIRE(LWS::Win32::SetMouseCapture(window, true) == LWS::Result::Success);
    REQUIRE(LWS::Win32::SetMouseCapture(other, true) == LWS::Result::InvalidState);
    REQUIRE_FALSE(other.IsCreated());
    REQUIRE(duringDestroy == LWS::Result::InvalidState);
    REQUIRE(LWS::Win32::SetMouseCapture(other, false) == LWS::Result::InvalidState);
    connection->Disconnect();
    REQUIRE_FALSE(connection->IsConnected());
}

TEST_CASE("Typed Win32 event connections are runtime validated", "[window][event][win32]")
{
    Context context;
    LWS::Window window(context.value);
    unsigned paints{};
    auto connection = LWS::Win32::Listen(window,
                                         [&](const LWS::Win32::PlatformEvent& event) -> std::optional<LRESULT>
                                         {
                                             paints += std::holds_alternative<LWS::Win32::PaintEvent>(event);
                                             return std::nullopt;
                                         });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(paints > 0);
}

TEST_CASE("Listener exceptions stop propagation and report through the context", "[window][event][exception][win32]")
{
    Context context;
    unsigned exceptions{};
    unsigned laterCalls{};
    context.value.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    LWS::Window window(context.value);
    auto throwing = window.Listen([](const LWS::AnyEvent&) -> LWS::EventResponse { throw 7; });
    auto later = window.Listen(
        [&](const LWS::AnyEvent&)
        {
            ++laterCalls;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(throwing.has_value());
    REQUIRE(later.has_value());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(exceptions == 1);
    REQUIRE(laterCalls == 0);
}

TEST_CASE("Unhandled close destroys and handled close retains", "[window][event][close][win32]")
{
    Context context;
    LWS::Window retained(context.value);
    auto connection = retained.Listen(
        [](const LWS::AnyEvent& event)
        {
            return std::holds_alternative<LWS::EventCloseRequested>(event) ? LWS::EventResponse::Handled
                                                                           : LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(retained.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(retained), WM_CLOSE, 0, 0);
    REQUIRE(retained.IsCreated());

    LWS::Window destroyed(context.value);
    REQUIRE(destroyed.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(destroyed), WM_CLOSE, 0, 0);
    REQUIRE_FALSE(destroyed.IsCreated());
}

TEST_CASE("Posted tasks execute FIFO on the context thread", "[platform][task][win32]")
{
    Context context;
    std::vector<int> order;
    REQUIRE(context.value.PostTask([&] { order.push_back(1); }) == LWS::Result::Success);
    REQUIRE(context.value.PostTask([&] { order.push_back(2); }) == LWS::Result::Success);
    Pump(context.value);
    REQUIRE(order == std::vector{1, 2});
}

TEST_CASE("Task exceptions report and later work continues", "[platform][exception][win32]")
{
    Context context;
    unsigned exceptions{};
    bool laterRan{};
    context.value.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    REQUIRE(context.value.PostTask([] { throw 7; }) == LWS::Result::Success);
    REQUIRE(context.value.PostTask([&] { laterRan = true; }) == LWS::Result::Success);
    Pump(context.value);
    REQUIRE(exceptions == 1);
    REQUIRE(laterRan);
}

TEST_CASE("Custom cursors validate and apply copied pixels", "[cursor][bitmap][win32]")
{
    const std::array pixels{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto cursor = LWS::Cursor::FromBitmap({.pixels = pixels, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(cursor.has_value());

    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMouseCursorVisible(false) == LWS::Result::Success);
    REQUIRE(window.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Arrow)) == LWS::Result::Success);
}

TEST_CASE("Timer can target windows in its context", "[timer][win32]")
{
    Context context;
    LWS::Window first(context.value);
    LWS::Window second(context.value);
    LWS::Window uncreated(context.value);
    REQUIRE(first.Create() == LWS::Result::Success);
    REQUIRE(second.Create() == LWS::Result::Success);
    LWS::Timer timer(context.value);
    REQUIRE(timer.SetTargetWindow(&uncreated) == LWS::Result::InvalidState);
    REQUIRE(timer.SetTargetWindow(&first) == LWS::Result::Success);
    timer.SetInterval(25);
    REQUIRE(timer.SetTargetWindow(nullptr) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 25);
    REQUIRE(timer.SetTargetWindow(&second) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 25);
}

TEST_CASE("Clipboard operations require an owned window", "[clipboard][win32]")
{
    Context context;
    LWS::Window window(context.value);
    LWS::Window uncreated(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Clipboard clipboard(context.value);
    const std::array<std::byte, 1> data{};
    REQUIRE(clipboard.SetClipboardData(uncreated, 1, data.data(), data.size()) != LWS::ClipboardResult::Success);
    REQUIRE(clipboard.SetClipboardData(window, 0, data.data(), data.size()) != LWS::ClipboardResult::Success);

    LWS::ListFileDialogFileNames files;
    REQUIRE(LWS::FileDialog::Show(LWS::FileDialogType::OpenFile, {}, {}, uncreated, {}, 1, {}, files) ==
            LWS::FileDialogResult::UnknownError);
}

TEST_CASE("Clipboard service ownership cannot be copied", "[clipboard][win32]")
{
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<LWS::Clipboard>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<LWS::Clipboard>);
}

TEST_CASE("Native resize limits measure the logical client area", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create(
                {.clientSize = {400, 300}, .styles = LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder}) ==
            LWS::Result::Success);
    const HWND handle = Hwnd(window);
    RECT outer{};
    RECT client{};
    REQUIRE(GetWindowRect(handle, &outer));
    REQUIRE(GetClientRect(handle, &client));
    const LONG borderWidth = outer.right - outer.left - client.right;
    const LONG borderHeight = outer.bottom - outer.top - client.bottom;
    const auto scale = window.GetClientAreaMetrics().Scale();
    REQUIRE(scale.has_value());
    REQUIRE(window.SetClientSizeLimits({{100, 120}, {900, 700}}) == LWS::Result::Success);
    MINMAXINFO limits{};
    SendMessageW(handle, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    REQUIRE(limits.ptMinTrackSize.x == static_cast<LONG>(std::lround(100 * scale->x)) + borderWidth);
    REQUIRE(limits.ptMinTrackSize.y == static_cast<LONG>(std::lround(120 * scale->y)) + borderHeight);
    REQUIRE(limits.ptMaxTrackSize.x == static_cast<LONG>(std::lround(900 * scale->x)) + borderWidth);
    REQUIRE(limits.ptMaxTrackSize.y == static_cast<LONG>(std::lround(700 * scale->y)) + borderHeight);
}

TEST_CASE("A shape replaces a custom cursor", "[cursor][win32]")
{
    const std::array pixels{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    const auto custom = LWS::Cursor::FromBitmap({.pixels = pixels, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(custom.has_value());
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMouseCursor(*custom) == LWS::Result::Success);
    REQUIRE(window.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Hand)) == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(window)),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_HAND));
}

TEST_CASE("Retrieved timer messages cannot invoke a replacement timer", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto timer = std::make_unique<LWS::Timer>(context.value);
    REQUIRE(timer->SetTargetWindow(&window) == LWS::Result::Success);
    timer->SetInterval(1);
    MSG tick{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PeekMessageW(&tick, Hwnd(window), WM_TIMER, WM_TIMER, PM_REMOVE) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(tick.message == WM_TIMER);
    timer.reset();
    unsigned calls{};
    LWS::Timer replacement(context.value);
    REQUIRE(replacement.SetTargetWindow(&window) == LWS::Result::Success);
    replacement.SetCallback([&] { ++calls; });
    replacement.SetInterval(1);
    DispatchMessageW(&tick);
    REQUIRE(calls == 0);
}

TEST_CASE("Timer callbacks can destroy their timer", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto timer = std::make_unique<LWS::Timer>(context.value);
    REQUIRE(timer->SetTargetWindow(&window) == LWS::Result::Success);
    timer->SetCallback([&] { timer.reset(); });
    timer->SetInterval(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (timer && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(timer);
}

TEST_CASE("High precision callbacks can destroy their timer", "[timer][precision][lifetime][win32]")
{
    Context context;
    std::unique_ptr<LWS::HighPrecisionTimer> timer;
    timer = std::make_unique<LWS::HighPrecisionTimer>(context.value, [&] { timer.reset(); });
    timer->SetDueTime(0);
    timer->SetRepeatInterval(1);
    timer->Enable(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (timer && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(timer);
}

TEST_CASE("Disabling a high precision timer discards pending ticks", "[timer][precision][win32]")
{
    Context context;
    unsigned calls{};
    LWS::HighPrecisionTimer timer(context.value, [&] { ++calls; });
    timer.SetDueTime(0);
    timer.SetRepeatInterval(1);
    timer.Enable(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    timer.Enable(false);
    Pump(context.value);
    REQUIRE(calls == 0);
    timer.SetRepeatInterval(0);
    timer.Enable(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!calls && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(calls == 1);
    REQUIRE_FALSE(timer.GetEnabled());
}

TEST_CASE("Independent UI threads own independent timer registrations", "[timer][thread][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    std::atomic_uint successes{};
    auto run = [&]
    {
        LWS::PlatformContext context;
        if (context.Init({.backend = LWS::BackendId::Win32}) != LWS::Result::Success)
            return;
        LWS::Window window(context);
        if (window.Create() != LWS::Result::Success)
            return;
        unsigned calls{};
        for (unsigned iteration = 0; iteration < 100; ++iteration)
        {
            LWS::Timer timer(context);
            if (timer.SetTargetWindow(&window) != LWS::Result::Success)
                return;
            timer.SetCallback([&] { ++calls; });
            timer.SetInterval(1);
            if (iteration == 99)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!calls && std::chrono::steady_clock::now() < deadline)
                {
                    std::ignore = context.ProcessMessages();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        if (calls > 0)
            ++successes;
    };
    std::jthread first(run);
    std::jthread second(run);
    first.join();
    second.join();
    REQUIRE(successes == 2);
}

TEST_CASE("Cursor visibility stays local to the window client area", "[cursor][visibility][win32]")
{
    Context context;
    LWS::Window hidden(context.value);
    LWS::Window visible(context.value);
    REQUIRE(hidden.Create() == LWS::Result::Success);
    REQUIRE(visible.Create() == LWS::Result::Success);
    const int initialVisibilityCount = ShowCursor(TRUE);
    ShowCursor(FALSE);
    REQUIRE(hidden.SetMouseCursorVisible(false) == LWS::Result::Success);
    SendMessageW(Hwnd(hidden), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(hidden)),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == nullptr);
    SendMessageW(Hwnd(visible), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(visible)),
                 MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_ARROW));
    const int finalVisibilityCount = ShowCursor(TRUE);
    ShowCursor(FALSE);
    REQUIRE(finalVisibilityCount == initialVisibilityCount);
    REQUIRE(hidden.SetMouseCursorVisible(true) == LWS::Result::Success);
}

TEST_CASE("Posted work cannot starve native window input", "[platform][task][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    bool keyReceived{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventKeyDown>(event))
            {
                keyReceived = true;
                context.value.RequestQuit();
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    unsigned batches{};
    std::function<void()> postAgain;
    postAgain = [&]
    {
        if (++batches < 1000)
            std::ignore = context.value.PostTask(postAgain);
        else
            context.value.RequestQuit();
    };
    REQUIRE(context.value.PostTask(postAgain) == LWS::Result::Success);
    REQUIRE(PostMessageW(Hwnd(window), WM_KEYDOWN, 'A', 0));
    context.value.RunMessageLoop();
    REQUIRE(keyReceived);
    REQUIRE(batches < 1000);
}

TEST_CASE("Destroying a timer target detaches it and invalidates retrieved ticks", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Timer timer(context.value);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    unsigned calls{};
    timer.SetCallback([&] { ++calls; });
    timer.SetInterval(1);
    MSG tick{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PeekMessageW(&tick, Hwnd(window), WM_TIMER, WM_TIMER, PM_REMOVE) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(tick.message == WM_TIMER);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    DispatchMessageW(&tick);
    REQUIRE(calls == 0);
    REQUIRE_NOTHROW(timer.SetInterval(2));
    REQUIRE(timer.GetInterval() == 2);
}

TEST_CASE("Initial client dimensions are preserved on each monitor", "[window][metrics][monitor][win32]")
{
    Context context;
    std::vector<RECT> monitors;
    REQUIRE(EnumDisplayMonitors(
        nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rectangle, LPARAM data) -> BOOL
        {
            reinterpret_cast<std::vector<RECT>*>(data)->push_back(*rectangle);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(&monitors)));
    const auto primary = context.value.GetPrimaryMonitor();
    REQUIRE(primary.has_value());
    const double systemScale = primary->contentScale.x;
    for (const RECT& monitor : monitors)
    {
        LWS::Window window(context.value);
        REQUIRE(
            window.Create({.position = LWS::Point{static_cast<int32_t>(std::lround((monitor.left + 50) / systemScale)),
                                                  static_cast<int32_t>(std::lround((monitor.top + 50) / systemScale))},
                           .clientSize = {400, 300}}) == LWS::Result::Success);
        const auto size = window.GetClientAreaMetrics().logical;
        INFO("monitor origin " << monitor.left << ", " << monitor.top << "; system scale " << systemScale
                               << "; actual logical client " << size.x << " x " << size.y << "; pixels "
                               << window.GetClientAreaMetrics().pixels->x << " x "
                               << window.GetClientAreaMetrics().pixels->y);
        REQUIRE(size == LWS::LogicalSize{400, 300});
    }
}

// These process-DPI scenarios must run in separate test processes before any normal context bootstrap.
TEST_CASE("Process bootstrap rejects a thread-only DPI override", "[.][bootstrap][win32]")
{
    REQUIRE(SetProcessDPIAware());
    const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    REQUIRE(previous != nullptr);
    const LWS::Result result = LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2});
    SetThreadDpiAwarenessContext(previous);
    REQUIRE(result == LWS::Result::Failure);
}

TEST_CASE("Process bootstrap recognizes process awareness behind a thread override", "[.][bootstrap][win32]")
{
    REQUIRE(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    REQUIRE(previous != nullptr);
    const LWS::Result result = LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2});
    SetThreadDpiAwarenessContext(previous);
    REQUIRE(result == LWS::Result::Success);
}

#endif
