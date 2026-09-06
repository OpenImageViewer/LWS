#include <catch2/catch_test_macros.hpp>

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Wayland/CursorBackendWayland.hpp>
    #include <LWS/Wayland/PlatformWayland.hpp>
    #include <LWS/Wayland/WindowBackendWayland.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/source/Wayland/internal/KeyCodeLinux.hpp>
    #include <LWS/source/Wayland/internal/CaptionRenderer.hpp>
    #include <LWS/source/Wayland/internal/WindowFrame.hpp>

    #include <array>
    #include <thread>
    #include <vector>

namespace
{
    class PlatformSession
    {
      public:

        PlatformSession() : result(LWS::Platform::init()) {}

        ~PlatformSession()
        {
            if (result == LWS::Result::Success)
            {
                LWS::Platform::shutdown();
            }
        }

        LWS::Result result;
    };
}  // namespace

TEST_CASE("Wayland translates non-contiguous Linux key codes", "[wayland][keyboard]")
{
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_A) == LWS::KeyCode::A);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_O) == LWS::KeyCode::O);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_Z) == LWS::KeyCode::Z);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_F10) == LWS::KeyCode::F10);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_F11) == LWS::KeyCode::F11);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_F12) == LWS::KeyCode::F12);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_KP0) == LWS::KeyCode::Numpad0);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_KP5) == LWS::KeyCode::Numpad5);
    REQUIRE(LWS::internal::keyCodeFromLinux(KEY_KP9) == LWS::KeyCode::Numpad9);
}

TEST_CASE("Wayland marks repeated key-down events", "[wayland][keyboard]")
{
    LWS::WindowBackendWayland backend;
    bool repeated = false;
    std::ignore = backend.addListener(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* key = std::get_if<LWS::EventKeyDown>(&event))
                repeated = key->key == LWS::KeyCode::Right && key->repeat;
            return true;
        });

    backend.handleKey(LWS::KeyCode::Right, true, true);
    REQUIRE(repeated);
}

TEST_CASE("Wayland reports its primary output", "[wayland][output]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
        SKIP("No Wayland compositor is available");

    const LWS::Platform::MonitorDesc primary = LWS::Platform::getPrimaryMonitor();
    if (primary.handle == 0)
        SKIP("No Wayland output is available");
    REQUIRE(primary.primary);
    REQUIRE(LWS::Platform::getMonitorInfo(primary.handle).handle == primary.handle);
}

TEST_CASE("Wayland cursor state can be configured before platform initialization", "[wayland][cursor]")
{
    LWS::CursorBackendWayland cursor;
    cursor.setCursorShape(LWS::CursorShape::SizeEW);
    cursor.setVisible(false);
    cursor.setVisible(true);
    REQUIRE(cursor.setCustomCursor({}) == LWS::Result::NotSupported);
}

TEST_CASE("Wayland frame gives caption controls priority over resize edges", "[wayland][window][frame]")
{
    using enum LWS::internal::WaylandFrameAction;
    using enum LWS::internal::WaylandResizeEdge;
    using LWS::internal::WaylandFrameConfig;
    using LWS::internal::waylandFrameHit;
    using LWS::internal::WaylandSurfaceRole;

    const LWS::Size size{800, LWS::internal::waylandCaptionHeight};
    const WaylandFrameConfig config{
        .surface = WaylandSurfaceRole::Caption,
        .detachedCaption = true,
        .closeButton = true,
        .resizeEnabled = true,
    };
    REQUIRE(waylandFrameHit({799, 0}, size, config).action == Close);
    REQUIRE(waylandFrameHit({799, 16}, size, config).action == Close);
    REQUIRE(waylandFrameHit({753, 0}, size, config) == LWS::internal::WaylandFrameHit{Resize, Top});
    REQUIRE(waylandFrameHit({753, 16}, size, config).action == Move);

    WaylandFrameConfig captionOnly = config;
    captionOnly.closeButton = false;
    REQUIRE(waylandFrameHit({799, 16}, size, captionOnly) == LWS::internal::WaylandFrameHit{Resize, Right});
    captionOnly.resizeEnabled = false;
    REQUIRE(waylandFrameHit({799, 16}, size, captionOnly).action == Move);
}

TEST_CASE("Wayland child input preserves its parent resize frame", "[wayland][window][frame]")
{
    using LWS::internal::waylandChildInputRect;

    const LWS::Rect sidebar = waylandChildInputRect({600, 0}, {200, 480}, {800, 480}, true);
    REQUIRE((sidebar.GetCorner(LLUtils::TopLeft) == LWS::Point{0, 0}));
    REQUIRE((sidebar.GetCorner(LLUtils::BottomRight) == LWS::Point{192, 472}));

    const LWS::Rect canvas = waylandChildInputRect({0, 0}, {600, 480}, {800, 480}, true);
    REQUIRE((canvas.GetCorner(LLUtils::TopLeft) == LWS::Point{8, 0}));
    REQUIRE((canvas.GetCorner(LLUtils::BottomRight) == LWS::Point{600, 472}));

    const LWS::Rect unframed = waylandChildInputRect({600, 0}, {200, 480}, {800, 480}, false);
    REQUIRE((unframed.GetCorner(LLUtils::TopLeft) == LWS::Point{0, 0}));
    REQUIRE((unframed.GetCorner(LLUtils::BottomRight) == LWS::Point{200, 480}));
}

TEST_CASE("Wayland caption renderer draws and clips the title", "[wayland][window][caption]")
{
    constexpr int32_t width = 200;
    constexpr int32_t titleRight = 150;
    constexpr uint32_t captionColor = 0xff303030U;
    std::vector<uint32_t> pixels(static_cast<size_t>(width) * LWS::internal::waylandCaptionHeight, captionColor);

    LWS::internal::renderCaptionTitle(pixels, width, "OpenImageViewer", titleRight);

    REQUIRE(std::ranges::any_of(pixels, [captionColor](uint32_t pixel) { return pixel != captionColor; }));
    for (int32_t y = 0; y < LWS::internal::waylandCaptionHeight; ++y)
    {
        const auto row = std::span(pixels).subspan(static_cast<size_t>(y) * width, width);
        REQUIRE(std::ranges::all_of(row.subspan(titleRight),
                                    [captionColor](uint32_t pixel) { return pixel == captionColor; }));
    }

    std::vector<uint32_t> narrowPixels(5 * LWS::internal::waylandCaptionHeight, captionColor);
    LWS::internal::renderCaptionTitle(narrowPixels, 5, "Title", 5);
    REQUIRE(std::ranges::all_of(narrowPixels, [captionColor](uint32_t pixel) { return pixel == captionColor; }));
}

TEST_CASE("Wayland frame identifies content resize edges", "[wayland][window][frame]")
{
    using enum LWS::internal::WaylandFrameAction;
    using enum LWS::internal::WaylandResizeEdge;
    using LWS::internal::WaylandFrameConfig;
    using LWS::internal::waylandFrameHit;
    using LWS::internal::WaylandSurfaceRole;

    const LWS::Size size{800, 600};
    const WaylandFrameConfig config{
        .surface = WaylandSurfaceRole::Content,
        .detachedCaption = true,
        .resizeEnabled = true,
    };
    REQUIRE(waylandFrameHit({0, 300}, size, config) == LWS::internal::WaylandFrameHit{Resize, Left});
    REQUIRE(waylandFrameHit({799, 300}, size, config) == LWS::internal::WaylandFrameHit{Resize, Right});
    REQUIRE(waylandFrameHit({400, 599}, size, config) == LWS::internal::WaylandFrameHit{Resize, Bottom});
    REQUIRE(waylandFrameHit({0, 599}, size, config) == LWS::internal::WaylandFrameHit{Resize, BottomLeft});
    REQUIRE(waylandFrameHit({799, 599}, size, config) == LWS::internal::WaylandFrameHit{Resize, BottomRight});
    REQUIRE(waylandFrameHit({400, 592}, size, config).action == Resize);
    REQUIRE(waylandFrameHit({400, 591}, size, config).action == Client);
    REQUIRE(waylandFrameHit({400, 0}, size, config).action == Client);
}

TEST_CASE("Wayland frame separates an in-surface caption from client input", "[wayland][window][frame]")
{
    using enum LWS::internal::WaylandFrameAction;
    using enum LWS::internal::WaylandResizeEdge;
    using LWS::internal::WaylandFrameConfig;
    using LWS::internal::waylandFrameHit;
    using LWS::internal::WaylandSurfaceRole;

    const LWS::Size size{800, 632};
    const WaylandFrameConfig config{
        .surface = WaylandSurfaceRole::Content,
        .closeButton = true,
        .resizeEnabled = true,
    };
    REQUIRE(waylandFrameHit({400, 0}, size, config) == LWS::internal::WaylandFrameHit{Resize, Top});
    REQUIRE(waylandFrameHit({400, 16}, size, config).action == Move);
    REQUIRE(waylandFrameHit({400, 32}, size, config).action == Client);
    REQUIRE(waylandFrameHit({799, 16}, size, config).action == Close);
}

TEST_CASE("Wayland frame disables chrome in non-interactive states", "[wayland][window][frame]")
{
    using LWS::internal::isWaylandResizeEnabled;
    using LWS::internal::waylandCaptionMode;
    using LWS::internal::WaylandCaptionMode;
    using LWS::internal::WaylandDecorationMode;

    constexpr LWS::WindowStyle styles = LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder;
    REQUIRE(isWaylandResizeEnabled(styles, LWS::WindowDisplayState::Restored, false, false));
    REQUIRE_FALSE(isWaylandResizeEnabled(styles, LWS::WindowDisplayState::Maximized, false, false));
    REQUIRE_FALSE(isWaylandResizeEnabled(styles, LWS::WindowDisplayState::Restored, true, false));
    REQUIRE_FALSE(isWaylandResizeEnabled(styles, LWS::WindowDisplayState::Restored, false, true));
    REQUIRE_FALSE(isWaylandResizeEnabled(LWS::WindowStyle::Caption, LWS::WindowDisplayState::Restored, false, false));
    REQUIRE(waylandCaptionMode(false, false, WaylandDecorationMode::None, true) == WaylandCaptionMode::ClientSide);
    REQUIRE(waylandCaptionMode(false, false, WaylandDecorationMode::ClientSide, true) ==
            WaylandCaptionMode::ClientSide);
    REQUIRE(waylandCaptionMode(false, false, WaylandDecorationMode::Pending, true) == WaylandCaptionMode::None);
    REQUIRE(waylandCaptionMode(false, false, WaylandDecorationMode::ServerSide, true) == WaylandCaptionMode::None);
    REQUIRE(waylandCaptionMode(false, true, WaylandDecorationMode::None, true) == WaylandCaptionMode::Detached);
    REQUIRE(waylandCaptionMode(false, true, WaylandDecorationMode::ClientSide, true) == WaylandCaptionMode::Detached);
    REQUIRE(waylandCaptionMode(false, true, WaylandDecorationMode::Pending, true) == WaylandCaptionMode::Detached);
    REQUIRE(waylandCaptionMode(false, true, WaylandDecorationMode::ServerSide, true) == WaylandCaptionMode::None);
    REQUIRE(waylandCaptionMode(true, true, WaylandDecorationMode::None, true) == WaylandCaptionMode::None);
}

TEST_CASE("Wayland creates an xdg-shell window", "[wayland][window]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
    {
        SKIP("No Wayland compositor is available");
    }

    LWS::Window window;
    LWS::WindowConfig config;
    config.visible = false;
    config.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton;
    REQUIRE(window.Create(config) == LWS::Result::Success);
    REQUIRE(LWS::Wayland::GetDisplay() != nullptr);
    REQUIRE(window.GetBackendId() == LWS::BackendId::Wayland);
    REQUIRE(window.GetHandle() != 0);

    window.SetMinMaxSize({320, 200}, {1920, 1080});
    REQUIRE((window.GetMinSize() == LWS::Size{320, 200}));
    REQUIRE((window.GetMaxSize() == LWS::Size{1920, 1080}));
}

TEST_CASE("Wayland pointer lock follows compositor support", "[wayland][pointer]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
        SKIP("No Wayland compositor is available");

    LWS::Window window;
    REQUIRE(window.Create() == LWS::Result::Success);
    if (!LWS::Platform::supports(LWS::Platform::Feature::PointerLock))
    {
        REQUIRE(window.SetPointerLocked(true) == LWS::Result::NotSupported);
        return;
    }

    REQUIRE(window.SetPointerLocked(true) == LWS::Result::Success);
    REQUIRE(window.SetPointerLocked(false) == LWS::Result::Success);
}

TEST_CASE("Wayland dispatches motion from the active pointer source", "[wayland][pointer]")
{
    LWS::WindowBackendWayland backend;
    std::vector<LWS::EventMouseMove> motions;
    std::ignore = backend.addListener(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* motion = std::get_if<LWS::EventMouseMove>(&event))
                motions.push_back(*motion);
            return true;
        });

    backend.handlePointerMotion({3, 4}, {3, 4}, LWS::internal::WaylandSurfaceRole::Content);
    backend.handlePointerLockState(true);
    backend.handlePointerMotion({5, 6}, {2, 2}, LWS::internal::WaylandSurfaceRole::Content);
    backend.handleRelativePointerMotion(0.5, 0.5);
    backend.handleRelativePointerMotion(0.75, 0.75);
    backend.handlePointerLockState(false);
    backend.handlePointerMotion({7, 8}, {2, 2}, LWS::internal::WaylandSurfaceRole::Content);

    REQUIRE(motions.size() == 3);
    REQUIRE((motions[0].delta == LWS::Point{3, 4}));
    REQUIRE((motions[1].delta == LWS::Point{1, 1}));
    REQUIRE((motions[2].delta == LWS::Point{2, 2}));
}

TEST_CASE("Wayland caption double-click toggles maximized state", "[wayland][window][caption]")
{
    LWS::WindowBackendWayland backend;
    backend.setWindowStyles(LWS::WindowStyle::Caption, true);
    backend.setDoubleClickMode(LWS::DoubleClickMode::Default);

    backend.handlePointerButton(LWS::MouseButton::Left, true, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                100);
    backend.handlePointerButton(LWS::MouseButton::Left, false, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                120);
    backend.handlePointerButton(LWS::MouseButton::Left, true, {102, 17}, LWS::internal::WaylandSurfaceRole::Content,
                                200);
    REQUIRE(backend.getDisplayState() == LWS::WindowDisplayState::Maximized);
    backend.handlePointerButton(LWS::MouseButton::Left, false, {102, 17}, LWS::internal::WaylandSurfaceRole::Content,
                                220);

    backend.handlePointerButton(LWS::MouseButton::Left, true, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                1000);
    backend.handlePointerButton(LWS::MouseButton::Left, false, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                1020);
    backend.handlePointerButton(LWS::MouseButton::Left, true, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                1100);
    REQUIRE(backend.getDisplayState() == LWS::WindowDisplayState::Restored);
    backend.handlePointerButton(LWS::MouseButton::Left, false, {100, 16}, LWS::internal::WaylandSurfaceRole::Content,
                                1120);
}

TEST_CASE("Wayland restores its windowed size after fullscreen", "[wayland][window][fullscreen]")
{
    LWS::WindowBackendWayland backend;
    backend.setSize({900, 700});

    backend.setFullScreenState(LWS::FullScreenState::SingleScreen);
    backend.handleToplevelConfigure({1920, 1080}, false, true);
    REQUIRE((backend.getClientSize() == LWS::Size{1920, 1080}));

    backend.setFullScreenState(LWS::FullScreenState::Windowed);
    backend.handleToplevelConfigure({}, false, false);
    REQUIRE((backend.getClientSize() == LWS::Size{900, 700}));
}

TEST_CASE("Wayland accepts a compositor-provided size after fullscreen", "[wayland][window][fullscreen]")
{
    LWS::WindowBackendWayland backend;
    backend.setSize({900, 700});
    backend.setFullScreenState(LWS::FullScreenState::SingleScreen);
    backend.handleToplevelConfigure({1920, 1080}, false, true);

    backend.setFullScreenState(LWS::FullScreenState::Windowed);
    backend.handleToplevelConfigure({1280, 720}, false, false);
    REQUIRE((backend.getClientSize() == LWS::Size{1280, 720}));
}

TEST_CASE("Wayland retains windowed size across overlapping fullscreen requests", "[wayland][window][fullscreen]")
{
    LWS::WindowBackendWayland backend;
    backend.setSize({900, 700});
    backend.setFullScreenState(LWS::FullScreenState::SingleScreen);
    backend.handleToplevelConfigure({1920, 1080}, false, true);

    backend.setFullScreenState(LWS::FullScreenState::Windowed);
    backend.setFullScreenState(LWS::FullScreenState::SingleScreen);
    backend.handleToplevelConfigure({1920, 1080}, false, true);
    backend.setFullScreenState(LWS::FullScreenState::Windowed);
    backend.handleToplevelConfigure({}, false, false);

    REQUIRE((backend.getClientSize() == LWS::Size{900, 700}));
}

TEST_CASE("Wayland restores its windowed size after maximization", "[wayland][window][caption]")
{
    LWS::WindowBackendWayland backend;
    backend.setSize({900, 700});
    backend.setDisplayState(LWS::WindowDisplayState::Maximized);
    backend.handleToplevelConfigure({1920, 1040}, true, false);

    backend.setDisplayState(LWS::WindowDisplayState::Restored);
    backend.handleToplevelConfigure({}, false, false);
    REQUIRE((backend.getClientSize() == LWS::Size{900, 700}));
}

TEST_CASE("Wayland embeds child windows as independently painted subsurfaces", "[wayland][window][child]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
    {
        SKIP("No Wayland compositor is available");
    }

    LWS::Window parent;
    LWS::WindowConfig parentConfig;
    parentConfig.styles = LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton;
    REQUIRE(parent.Create(parentConfig) == LWS::Result::Success);

    int paintEvents = 0;
    LWS::Window child;
    child.SetParent(&parent);
    std::ignore = child.AddEventListener(
        [&](const LWS::AnyEvent& event)
        {
            paintEvents += std::holds_alternative<LWS::EventPaint>(event) ? 1 : 0;
            return true;
        });
    const LWS::WindowConfig childConfig{
        .position = {7, 11},
        .size = {320, 200},
        .styles = LWS::WindowStyle::ChildWindow,
        .eraseBackground = false,
    };
    REQUIRE(child.Create(childConfig) == LWS::Result::Success);
    REQUIRE(child.GetParent() == &parent);
    REQUIRE((child.GetPosition() == LWS::Point{7, 11}));
    REQUIRE((child.GetClientSize() == LWS::Size{320, 200}));

    child.SetPosition({13, 17});
    child.SetVisible(true);
    REQUIRE((child.GetPosition() == LWS::Point{13, 17}));
    REQUIRE(paintEvents == 1);

    child.SetVisible(false);
    REQUIRE_FALSE(child.GetVisible());
    child.SetVisible(true);
    REQUIRE(child.GetVisible());
    REQUIRE(paintEvents == 2);
}

TEST_CASE("Wayland dispatches resize events for programmatic child sizes", "[wayland][window][child]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
        SKIP("No Wayland compositor is available");

    LWS::Window parent;
    REQUIRE(parent.Create() == LWS::Result::Success);

    int resizeEvents = 0;
    LWS::Size resizedTo{};
    LWS::Window child;
    child.SetParent(&parent);
    std::ignore = child.AddEventListener(
        [&](const LWS::AnyEvent& event)
        {
            if (const auto* resize = std::get_if<LWS::EventResize>(&event))
            {
                ++resizeEvents;
                resizedTo = resize->newClientSize;
            }
            return true;
        });
    REQUIRE(child.Create({.styles = LWS::WindowStyle::ChildWindow}) == LWS::Result::Success);

    child.SetSize({320, 480});
    REQUIRE(resizeEvents == 1);
    REQUIRE((resizedTo == LWS::Size{320, 480}));

    child.SetSize({320, 480});
    REQUIRE(resizeEvents == 1);

    child.SetPlacement({.position = {17, 23}, .size = {400, 500}});
    REQUIRE((child.GetPosition() == LWS::Point{17, 23}));
    REQUIRE(resizeEvents == 2);
    REQUIRE((resizedTo == LWS::Size{400, 500}));

    child.SetPlacement({.position = {17, 23}, .size = {400, 500}});
    REQUIRE(resizeEvents == 2);
}

TEST_CASE("Wayland requests the first paint without background erasure", "[wayland][window]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
    {
        SKIP("No Wayland compositor is available");
    }

    bool painted = false;
    LWS::Window window;
    std::ignore = window.AddEventListener(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event))
            {
                painted = true;
                LWS::Platform::requestQuit();
            }
            return true;
        });

    LWS::WindowConfig config;
    config.visible = true;
    config.eraseBackground = false;
    REQUIRE(window.Create(config) == LWS::Result::Success);

    LWS::HighPrecisionTimer timeout([] { LWS::Platform::requestQuit(); });
    timeout.SetDueTime(1000);
    timeout.SetRepeatInterval(0);
    timeout.Enable(true);
    LWS::Platform::runMessageLoop();
    timeout.Enable(false);

    REQUIRE(painted);
}

TEST_CASE("Wayland presents a software bitmap on a child surface", "[wayland][window][bitmap]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
        SKIP("No Wayland compositor is available");

    LWS::Window parent;
    REQUIRE(parent.Create() == LWS::Result::Success);

    constexpr uint32_t width = 32;
    constexpr uint32_t height = 24;
    std::array<std::byte, width * height * 4U> pixels{};
    bool presented = false;
    LWS::Window child;
    child.SetParent(&parent);
    std::ignore = child.AddEventListener(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event))
            {
                presented = child.PresentBitmap({
                                .pixels = pixels,
                                .format = LWS::BitmapPixelFormat::Bgra8Premultiplied,
                                .rowOrder = LWS::BitmapRowOrder::TopDown,
                                .width = width,
                                .height = height,
                                .rowPitch = 0,
                            }) == LWS::Result::Success;
            }
            return true;
        });
    const LWS::WindowConfig config{
        .size = {static_cast<int32_t>(width), static_cast<int32_t>(height)},
        .styles = LWS::WindowStyle::ChildWindow,
        .visible = true,
        .eraseBackground = false,
    };
    REQUIRE(child.Create(config) == LWS::Result::Success);
    REQUIRE(presented);
}

TEST_CASE("Wayland timers wake the UI loop", "[wayland][timer]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
    {
        SKIP("No Wayland compositor is available");
    }

    const std::thread::id uiThread = std::this_thread::get_id();
    bool firedOnUiThread = false;
    LWS::HighPrecisionTimer timer(
        [&]
        {
            firedOnUiThread = std::this_thread::get_id() == uiThread;
            LWS::Platform::requestQuit();
        });
    timer.SetDueTime(10);
    timer.SetRepeatInterval(0);
    timer.Enable(true);
    LWS::Platform::runMessageLoop();
    timer.Enable(false);

    REQUIRE(firedOnUiThread);
}

TEST_CASE("Wayland tasks wake and execute on the UI thread", "[wayland][task]")
{
    PlatformSession platform;
    if (platform.result != LWS::Result::Success)
    {
        SKIP("No Wayland compositor is available");
    }

    const std::thread::id uiThread = std::this_thread::get_id();
    bool firedOnUiThread = false;
    std::thread worker(
        [&]
        {
            LWS::Wayland::PostTask(
                [&]
                {
                    firedOnUiThread = std::this_thread::get_id() == uiThread;
                    LWS::Platform::requestQuit();
                });
        });
    worker.join();
    LWS::Platform::runMessageLoop();

    REQUIRE(firedOnUiThread);
}

#endif
