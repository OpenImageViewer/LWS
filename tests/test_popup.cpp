#ifdef LWS_PLATFORM_WIN32

    #include <catch2/catch_test_macros.hpp>

    #include <LWS/Platform.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/Window.hpp>

    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <cmath>
    #include <optional>
    #include <variant>

namespace
{
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

    RECT WindowRectangle(LWS::Window& window)
    {
        RECT rectangle{};
        REQUIRE(GetWindowRect(Hwnd(window), &rectangle));
        return rectangle;
    }

    LWS::PopupPlacement Placement(LWS::PopupGravity gravity = LWS::PopupGravity::DownRight,
                                  bool adjustToScreen = false)
    {
        return LWS::PopupPlacement{
            .anchor = {40, 30},
            .gravity = gravity,
            .offset = {},
            .size = {200, 150},
            .adjustToScreen = adjustToScreen,
        };
    }
}  // namespace

TEST_CASE("Window popup validation rejects unsupported configuration", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);

    auto rejected = [&](const LWS::WindowConfig& config)
    {
        LWS::Window popup(context.value);
        REQUIRE(popup.Create(config) == LWS::Result::InvalidArgument);
        REQUIRE_FALSE(popup.IsCreated());
    };

    LWS::WindowConfig config{.parent = &parent, .popupPlacement = Placement()};

    config.parent = nullptr;
    rejected(config);
    config.parent = &parent;

    config.position = LWS::Point{10, 10};
    rejected(config);
    config.position.reset();

    config.styles = LWS::WindowStyle::Caption;
    rejected(config);
    config.styles = {};

    config.showState = LWS::WindowShowState::Maximized;
    rejected(config);
    config.showState = LWS::WindowShowState::Restored;

    config.alwaysOnTop = true;
    rejected(config);
    config.alwaysOnTop = false;

    config.transparent = true;
    rejected(config);
    config.transparent = false;

    config.dragAndDropEnabled = true;
    rejected(config);
    config.dragAndDropEnabled = false;

    config.clientSizeLimits = {.minimum = {100, 100}};
    rejected(config);
    config.clientSizeLimits = {};

    config.popupPlacement->size = {200, 0};
    rejected(config);
    config.popupPlacement->size = {200, 150};

    LWS::Window uncreated(context.value);
    LWS::Window dependent(context.value);
    REQUIRE(dependent.Create({.parent = &uncreated, .popupPlacement = Placement()}) == LWS::Result::InvalidState);

    LWS::Window popup(context.value);
    REQUIRE(popup.Create(config) == LWS::Result::Success);
    REQUIRE(popup.IsPopup());
    LWS::Window plain(context.value);
    REQUIRE(plain.Create({.clientSize = {200, 150}}) == LWS::Result::Success);
    REQUIRE_FALSE(plain.IsPopup());
}

TEST_CASE("Window popup is an owned non-activating top-level", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);
    LWS::Window popup(context.value);
    REQUIRE(popup.Create({.parent = &parent, .popupPlacement = Placement()}) == LWS::Result::Success);

    const HWND handle = Hwnd(popup);
    const LONG_PTR style = GetWindowLongPtrW(handle, GWL_STYLE);
    REQUIRE((style & WS_CHILD) == 0);
    REQUIRE((style & WS_POPUP) != 0);
    REQUIRE((style & WS_VISIBLE) == 0);
    REQUIRE(GetWindow(handle, GW_OWNER) == Hwnd(parent));
    const LONG_PTR extendedStyle = GetWindowLongPtrW(handle, GWL_EXSTYLE);
    REQUIRE((extendedStyle & WS_EX_TOOLWINDOW) != 0);
    REQUIRE((extendedStyle & WS_EX_NOACTIVATE) != 0);

    REQUIRE(popup.SetVisible(true) == LWS::Result::Success);
    REQUIRE((GetWindowLongPtrW(handle, GWL_STYLE) & WS_VISIBLE) != 0);
    REQUIRE(popup.SetVisible(false) == LWS::Result::Success);
    REQUIRE((GetWindowLongPtrW(handle, GWL_STYLE) & WS_VISIBLE) == 0);
}

TEST_CASE("Window popup placement resolves the gravity corner at the anchor", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);

    auto positioned = [&](LWS::PopupGravity gravity, LWS::Point offset)
    {
        LWS::Window popup(context.value);
        LWS::PopupPlacement placement = Placement(gravity);
        placement.offset = offset;
        REQUIRE(popup.Create({.parent = &parent, .popupPlacement = placement}) == LWS::Result::Success);
        REQUIRE(popup.GetClientAreaMetrics().logical == LWS::LogicalSize{200, 150});
        const LWS::WindowPlacement resolved = popup.GetPlacement();
        REQUIRE(resolved.position.has_value());
        return *resolved.position;
    };

    // The popup corner opposite the extension direction is pinned to the anchor.
    REQUIRE(positioned(LWS::PopupGravity::DownRight, {}) == LWS::Point{40, 30});
    REQUIRE(positioned(LWS::PopupGravity::DownLeft, {}) == LWS::Point{-160, 30});
    REQUIRE(positioned(LWS::PopupGravity::UpRight, {}) == LWS::Point{40, -120});
    REQUIRE(positioned(LWS::PopupGravity::UpLeft, {}) == LWS::Point{-160, -120});
    // The offset shifts the anchor point before the corner is pinned to it.
    REQUIRE(positioned(LWS::PopupGravity::DownRight, {5, -7}) == LWS::Point{45, 23});
    REQUIRE(positioned(LWS::PopupGravity::UpLeft, {5, -7}) == LWS::Point{-155, -127});
}

TEST_CASE("Window popup placement requests restate the anchor and size", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);

    LWS::Window downRight(context.value);
    REQUIRE(downRight.Create({.parent = &parent, .popupPlacement = Placement()}) == LWS::Result::Success);
    REQUIRE(downRight.RequestPlacement({.position = LWS::Point{150, 60}}) == LWS::Result::Success);
    REQUIRE(downRight.GetPlacement().position == LWS::Point{150, 60});
    REQUIRE(downRight.RequestPlacement({.clientSize = LWS::LogicalSize{300, 220}}) == LWS::Result::Success);
    REQUIRE(downRight.GetClientAreaMetrics().logical == LWS::LogicalSize{300, 220});
    REQUIRE(downRight.GetPlacement().position == LWS::Point{150, 60});

    // Resizing an up-left popup moves the far corner while the anchor stays put.
    LWS::Window upLeft(context.value);
    REQUIRE(upLeft.Create({.parent = &parent, .popupPlacement = Placement(LWS::PopupGravity::UpLeft)}) ==
            LWS::Result::Success);
    REQUIRE(upLeft.GetPlacement().position == LWS::Point{-160, -120});
    REQUIRE(upLeft.RequestPlacement({.clientSize = LWS::LogicalSize{300, 220}}) == LWS::Result::Success);
    REQUIRE(upLeft.GetPlacement().position == LWS::Point{-260, -190});
}

TEST_CASE("Window popup placement adjusts to the monitor work area", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);
    POINT clientOrigin{0, 0};
    REQUIRE(ClientToScreen(Hwnd(parent), &clientOrigin));

    LWS::Window probe(context.value);
    REQUIRE(probe.Create({.parent = &parent, .popupPlacement = Placement()}) == LWS::Result::Success);
    const std::optional<LWS::ContentScale> probeScale = probe.GetClientAreaMetrics().Scale();
    REQUIRE(probeScale.has_value());
    const double scale = probeScale->x;
    REQUIRE(scale > 0.0);

    MONITORINFO monitor{sizeof(MONITORINFO)};
    REQUIRE(GetMonitorInfoW(
        MonitorFromPoint(POINT{clientOrigin.x + static_cast<int32_t>(std::lround(40 * scale)), clientOrigin.y},
                         MONITOR_DEFAULTTONEAREST),
        &monitor));
    const RECT work = monitor.rcWork;

    // A popup that cannot fit to the right flips toward the anchor's opposite side.
    LWS::PopupPlacement flipped = Placement();
    flipped.adjustToScreen = true;
    flipped.anchor = {static_cast<int32_t>(std::lround((work.right - 50 - clientOrigin.x) / scale)), 20};
    LWS::Window flip(context.value);
    REQUIRE(flip.Create({.parent = &parent, .popupPlacement = flipped}) == LWS::Result::Success);
    const RECT flippedRect = WindowRectangle(flip);
    REQUIRE(std::abs(flippedRect.right - (work.right - 50)) <= 1);
    REQUIRE(flippedRect.left >= work.left);
    REQUIRE(flippedRect.bottom <= work.bottom);

    // A popup wider than the work area clamps instead of flipping off the far side.
    LWS::PopupPlacement oversized = Placement();
    oversized.adjustToScreen = true;
    oversized.size = {static_cast<int32_t>((work.right - work.left) / scale) + 100, 150};
    oversized.anchor = {static_cast<int32_t>(std::lround((work.right - 40 - clientOrigin.x) / scale)), 20};
    LWS::Window clamp(context.value);
    REQUIRE(clamp.Create({.parent = &parent, .popupPlacement = oversized}) == LWS::Result::Success);
    const RECT clampedRect = WindowRectangle(clamp);
    REQUIRE(clampedRect.left == work.left);
    REQUIRE(clampedRect.right > work.right);
}

TEST_CASE("Window popup rejects owner-level requests and follows its parent", "[window][popup][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);
    LWS::Window popup(context.value);
    unsigned destroyed{};
    unsigned dismissed{};
    auto connection = popup.Listen(
        [&](const LWS::AnyEvent& event)
        {
            destroyed += std::holds_alternative<LWS::EventWindowDestroyed>(event);
            dismissed += std::holds_alternative<LWS::EventPopupDismissed>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(popup.Create({.parent = &parent, .popupPlacement = Placement()}) == LWS::Result::Success);

    REQUIRE(popup.SetWindowStyles(LWS::WindowStyle::Caption) == LWS::Result::NotSupported);
    REQUIRE(popup.SetClientSizeLimits({.minimum = {100, 100}}) == LWS::Result::NotSupported);
    REQUIRE(popup.Center(LWS::CenterTarget::CurrentMonitor) == LWS::Result::NotSupported);
    REQUIRE(popup.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::NotSupported);
    REQUIRE(popup.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::NotSupported);
    REQUIRE(popup.SetAlwaysOnTop(true) == LWS::Result::NotSupported);
    REQUIRE(popup.SetTransparent(true) == LWS::Result::NotSupported);
    REQUIRE(popup.SetDragAndDropEnabled(true) == LWS::Result::NotSupported);
    REQUIRE(popup.RequestActivation() == LWS::Result::NotSupported);
    REQUIRE(popup.SetTitle(L"menu") == LWS::Result::Success);
    REQUIRE(popup.SetEraseBackground(false) == LWS::Result::Success);

    // Windows popups dismiss by application request only, so no popup-dismissed event exists here.
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE_FALSE(popup.IsCreated());
    REQUIRE(destroyed == 1);
    REQUIRE(dismissed == 0);
}

#endif
