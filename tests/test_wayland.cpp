#ifdef LWS_PLATFORM_WAYLAND

    #include <catch2/catch_test_macros.hpp>

    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Wayland/WindowExtensions.hpp>
    #include <LWS/Window.hpp>

    #include <array>
    #include <chrono>
    #include <cstdlib>
    #include <ranges>
    #include <thread>
    #include <variant>
    #include <vector>

namespace
{
    bool Initialize(LWS::PlatformContext& context)
    {
        const LWS::Result result = context.Init({.backend = LWS::BackendId::Wayland});
        if (result != LWS::Result::Success)
            SKIP("A Wayland compositor is not available");
        return true;
    }
}  // namespace

TEST_CASE("Wayland is the compiled backend", "[platform][wayland]")
{
    const auto backends = LWS::PlatformContext::GetAvailableBackends();
    REQUIRE(std::ranges::find(backends, LWS::BackendId::Wayland) != backends.end());
    REQUIRE(std::ranges::find(backends, LWS::BackendId::X11) == backends.end());
}

TEST_CASE("Wayland context is explicit and one-shot", "[platform][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    REQUIRE(context.GetBackendId() == LWS::BackendId::Wayland);
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::InvalidState);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::InvalidState);
}

TEST_CASE("Wayland monitor queries are scoped to the context", "[platform][monitor][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    const auto primary = context.GetPrimaryMonitor(true);
    REQUIRE(primary.has_value());
    if (primary->handle != 0)
        REQUIRE(context.GetMonitorInfo(primary->handle)->handle == primary->handle);
}

TEST_CASE("Wayland window exposes stable typed native objects", "[window][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(LWS::Wayland::SetAppId(window, "io.github.openimageviewer") == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::Success);
    const auto surface = LWS::Wayland::GetSurface(window);
    const auto display = LWS::Wayland::GetDisplay(window);
    REQUIRE(surface.has_value());
    REQUIRE(display.has_value());
    REQUIRE(*surface != nullptr);
    REQUIRE(*display != nullptr);
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(window) == surface);
}

TEST_CASE("Wayland top-level positions and multi-monitor fullscreen are unsupported", "[window][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetPosition({20, 30}) == LWS::Result::NotSupported);
    REQUIRE_FALSE(window.GetPosition().has_value());
    REQUIRE(window.SetWindowMode(LWS::WindowMode::FullscreenAllMonitors) == LWS::Result::NotSupported);
    REQUIRE(window.SetAlwaysOnTop(true) == LWS::Result::NotSupported);
    REQUIRE(window.BeginWindowDrag(LWS::WindowDragOperation::Move) == LWS::Result::NotSupported);
}

TEST_CASE("Wayland child containment uses parent configuration", "[window][parent][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window parent(context);
    LWS::Window child(context);
    REQUIRE(parent.Create({.visible = true}) == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .clientSize = {320, 200}}) == LWS::Result::Success);
    REQUIRE(child.GetParent() == &parent);
    const auto surface = LWS::Wayland::GetSurface(child);
    REQUIRE(surface.has_value());
    REQUIRE(child.SetVisible(true) == LWS::Result::Success);
    REQUIRE(child.SetVisible(false) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(child) == surface);
    REQUIRE(child.SetVisible(true) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetSurface(child) == surface);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE_FALSE(child.IsCreated());
}

TEST_CASE("Wayland custom cursors fail without changing standard selection", "[cursor][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Hand)) == LWS::Result::Success);
    const std::array<std::byte, 4> pixel{};
    auto custom = LWS::Cursor::FromBitmap({.pixels = pixel, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(custom.has_value());
    REQUIRE(window.SetMouseCursor(*custom) == LWS::Result::NotSupported);
}

TEST_CASE("Wayland task wake executes FIFO", "[platform][task][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    std::vector<int> order;
    REQUIRE(context.PostTask([&] { order.push_back(1); }) == LWS::Result::Success);
    REQUIRE(context.PostTask([&] { order.push_back(2); }) == LWS::Result::Success);
    std::ignore = context.ProcessMessages();
    REQUIRE(order == std::vector{1, 2});
}

TEST_CASE("Wayland timer callbacks marshal through the context", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    bool fired{};
    LWS::HighPrecisionTimer timer(context, [&] { fired = true; });
    timer.SetDueTime(1);
    timer.SetRepeatInterval(0);
    timer.Enable(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (!fired && std::chrono::steady_clock::now() < deadline)
        std::ignore = context.ProcessMessages();
    REQUIRE(fired);
}

TEST_CASE("Wayland timer restart discards an already queued expiration", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    unsigned callbacks{};
    LWS::HighPrecisionTimer timer(context, [&] { ++callbacks; });
    timer.SetDueTime(1);
    timer.SetRepeatInterval(0);
    timer.Enable(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    timer.Enable(false);
    timer.SetDueTime(60'000);
    timer.Enable(true);
    std::ignore = context.ProcessMessages();
    REQUIRE(callbacks == 0);
}

TEST_CASE("Wayland timer null target detaches and retains its interval", "[timer][wayland]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    unsigned callbacks{};
    LWS::Timer timer(context);
    timer.SetCallback([&] { ++callbacks; });
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    timer.SetInterval(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    REQUIRE(timer.SetTargetWindow(nullptr) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 1);
    std::ignore = context.ProcessMessages();
    REQUIRE(callbacks == 0);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (callbacks == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(callbacks > 0);
}

#endif
