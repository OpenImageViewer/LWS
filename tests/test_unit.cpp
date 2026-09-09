// Unit tests for LWS types that don't require a real window or platform init.
#include <catch2/catch_test_macros.hpp>

#include <LWS/Bitmap.hpp>
#include <LWS/Event.hpp>
#include <LWS/Cursor.hpp>
#include <LWS/KeyCode.hpp>
#include <LWS/MouseButton.hpp>
#include <LWS/NotificationIconGroup.hpp>
#include <LWS/Result.hpp>
#include <LWS/Window.hpp>
#include <LWS/WindowShowState.hpp>
#include <LLUtils/Exception.h>

#include "LWS/source/internal/ListenerState.hpp"
#include "LWS/source/Wayland/internal/WheelDeltaFrame.hpp"
#ifdef LWS_PLATFORM_WAYLAND
    #include "LWS/source/Wayland/internal/UriList.hpp"
#endif

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

// ---------------------------------------------------------------------------
// Result enum
// ---------------------------------------------------------------------------
TEST_CASE("Result enum covers all expected values", "[result]")
{
    REQUIRE(static_cast<int>(LWS::Result::Success) == 0);
    REQUIRE(LWS::Result::Failure != LWS::Result::Success);
    REQUIRE(LWS::Result::InvalidArgument != LWS::Result::Success);
    REQUIRE(LWS::Result::InvalidState != LWS::Result::Success);
    REQUIRE(LWS::Result::NotSupported != LWS::Result::Success);
    REQUIRE(LWS::Result::AlreadyCreated != LWS::Result::Success);
    REQUIRE(LWS::Result::NotCreated != LWS::Result::Success);
    REQUIRE(LWS::Result::PlatformNotInitialized != LWS::Result::Success);
    REQUIRE(LWS::Result::IncompatibleThreadApartment != LWS::Result::Success);
}

// ---------------------------------------------------------------------------
// WindowConfig defaults
// ---------------------------------------------------------------------------
TEST_CASE("WindowConfig has expected defaults", "[config]")
{
    STATIC_REQUIRE_FALSE(std::is_convertible_v<LWS::LogicalSize, LWS::PixelSize>);
    STATIC_REQUIRE_FALSE(std::is_convertible_v<LWS::PixelSize, LWS::LogicalSize>);
    LWS::WindowConfig cfg{};
    REQUIRE(cfg.clientSize.x == 800);
    REQUIRE(cfg.clientSize.y == 600);
    REQUIRE(cfg.visible == false);
    REQUIRE(cfg.eraseBackground == true);
    REQUIRE(cfg.alwaysOnTop == false);
    REQUIRE(cfg.transparent == false);
    REQUIRE(cfg.minClientSize.x == 0);
    REQUIRE(cfg.minClientSize.y == 0);
    REQUIRE(cfg.maxClientSize.x == 0);
    REQUIRE(cfg.maxClientSize.y == 0);
    REQUIRE(cfg.styles == LWS::WindowStyle::NoStyle);
    REQUIRE(cfg.showState == LWS::WindowShowState::Restored);
}

TEST_CASE("Cursor values are immutable cheap copies", "[cursor][lifetime]")
{
    const LWS::Cursor cursor = LWS::Cursor::FromShape(LWS::CursorShape::Hand);
    const LWS::Cursor copy = cursor;
    STATIC_REQUIRE(std::is_copy_constructible_v<LWS::Cursor>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<LWS::Cursor>);
    std::ignore = copy;
}

TEST_CASE("Cursor validates custom bitmap hotspots", "[cursor][bitmap]")
{
    const std::array<std::byte, 4> pixel{};
    const LWS::BitmapBuffer bitmap{.pixels = pixel, .width = 1, .height = 1, .rowPitch = 4};
    REQUIRE(LWS::Cursor::FromBitmap(bitmap, {0, 0}).has_value());
    REQUIRE(LWS::Cursor::FromBitmap(bitmap, {1, 0}).error() == LWS::Result::InvalidArgument);
}

// ---------------------------------------------------------------------------
// AnyEvent variant dispatch
// ---------------------------------------------------------------------------
TEST_CASE("AnyEvent variant holds a coherent client area and can be visited", "[event]")
{
    LWS::AnyEvent ev = LWS::EventClientAreaSizeChanged{{{1280, 720}, {2560, 1440}}};
    bool visited = false;
    std::visit(
        [&](const auto& e)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, LWS::EventClientAreaSizeChanged>)
            {
                REQUIRE(e.size.logical.x == 1280);
                REQUIRE(e.size.pixels.x == 2560);
                REQUIRE(e.size.Scale().x == 2.0);
                visited = true;
            }
        },
        ev);
    REQUIRE(visited);
}

TEST_CASE("AnyEvent variant holds EventKeyDown", "[event]")
{
    LWS::AnyEvent ev = LWS::EventKeyDown{LWS::KeyCode::A, false};
    REQUIRE(std::holds_alternative<LWS::EventKeyDown>(ev));
    REQUIRE(std::get<LWS::EventKeyDown>(ev).key == LWS::KeyCode::A);
    REQUIRE(std::get<LWS::EventKeyDown>(ev).repeat == false);
}

TEST_CASE("AnyEvent variant holds EventMouseButton", "[event]")
{
    LWS::AnyEvent ev = LWS::EventMouseButton{LWS::MouseButton::Left, true, {100, 200}};
    REQUIRE(std::holds_alternative<LWS::EventMouseButton>(ev));
    const auto& mb = std::get<LWS::EventMouseButton>(ev);
    REQUIRE(mb.button == LWS::MouseButton::Left);
    REQUIRE(mb.pressed == true);
    REQUIRE(mb.position.x == 100);
}

// ---------------------------------------------------------------------------
// EventConnection — type guarantees
// ---------------------------------------------------------------------------
TEST_CASE("EventConnection is move-only", "[event]")
{
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<LWS::EventConnection>);
    STATIC_REQUIRE(std::is_nothrow_move_constructible_v<LWS::EventConnection>);
    const LWS::EventConnection connection;
    REQUIRE_FALSE(connection.IsConnected());
}

// ---------------------------------------------------------------------------
// WindowShowState enum
// ---------------------------------------------------------------------------
TEST_CASE("WindowShowState enum values are distinct", "[state]")
{
    REQUIRE(LWS::WindowShowState::Restored != LWS::WindowShowState::Minimized);
    REQUIRE(LWS::WindowShowState::Minimized != LWS::WindowShowState::Maximized);
    REQUIRE(LWS::WindowShowState::Restored != LWS::WindowShowState::Maximized);
}

// ---------------------------------------------------------------------------
// WindowPlacement struct
// ---------------------------------------------------------------------------
TEST_CASE("WindowPlacement defaults to empty logical geometry", "[placement]")
{
    LWS::WindowPlacement p;
    REQUIRE_FALSE(p.position.has_value());
    REQUIRE(p.clientSize.x == 0);
    REQUIRE(p.clientSize.y == 0);
}

// ---------------------------------------------------------------------------
// BackendId enum
// ---------------------------------------------------------------------------
TEST_CASE("BackendId has expected Win32 value", "[backend]")
{
    REQUIRE(LWS::BackendId::Win32 != LWS::BackendId::Undefined);
    REQUIRE(LWS::BackendId::Wayland != LWS::BackendId::Win32);
}

#ifdef LWS_PLATFORM_X11
TEST_CASE("X11 scaffold exposes no usable backend", "[backend][x11]")
{
    REQUIRE(LWS::PlatformContext::GetAvailableBackends().empty());
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::X11}) == LWS::Result::NotSupported);
}
#endif

TEST_CASE("Mouse wheel events expose normalized steps", "[input]")
{
    REQUIRE((LWS::EventMouseWheel{120, {}}.steps() == 1.0));
    REQUIRE((LWS::EventMouseWheel{-60, {}}.steps() == -0.5));
    REQUIRE((LWS::EventMouseWheel{30, {}}.steps() == 0.25));
    REQUIRE((LWS::EventMouseWheel{240, {}}.steps() == 2.0));
}

TEST_CASE("Wayland wheel frames prefer exact detent data", "[input][wayland]")
{
    STATIC_REQUIRE_FALSE(LWS::internal::WheelDeltaFrame::usesPointerFrame(4));
    STATIC_REQUIRE(LWS::internal::WheelDeltaFrame::usesPointerFrame(5));

    LWS::internal::WheelDeltaFrame frame;
    frame.addWaylandAxis(15.0);
    frame.addWaylandDiscrete(2);
    frame.addWaylandValue120(60);

    REQUIRE(frame.takeDelta() == -60);
    REQUIRE_FALSE(frame.takeDelta().has_value());
}

TEST_CASE("Wayland wheel frames aggregate and normalize fallbacks", "[input][wayland]")
{
    LWS::internal::WheelDeltaFrame frame;

    frame.addWaylandValue120(30);
    frame.addWaylandValue120(90);
    REQUIRE(frame.takeDelta() == -120);

    frame.addWaylandDiscrete(-2);
    REQUIRE(frame.takeDelta() == 240);

    frame.addWaylandAxis(-5.0);
    REQUIRE(frame.takeDelta() == 60);

    frame.addWaylandAxis(-0.03);
    REQUIRE_FALSE(frame.takeDelta().has_value());

    frame.addWaylandValue120(120);
    frame.clear();
    REQUIRE_FALSE(frame.takeDelta().has_value());
}

#ifdef LWS_PLATFORM_WAYLAND
TEST_CASE("Wayland URI lists decode local file paths", "[input][wayland][drag-drop]")
{
    const auto paths = LWS::internal::parseUriList(
        "# files\r\nfile:///tmp/first%20image.png\r\nFILE:///tmp/%D7%AA%D7%9E%D7%95%D7%A0%D7%94.png\n"
        "file://localhost/tmp/last.png\n");

    REQUIRE(paths == std::vector<std::filesystem::path>{"/tmp/first image.png", "/tmp/תמונה.png", "/tmp/last.png"});
}

TEST_CASE("Wayland URI lists ignore unsupported entries", "[input][wayland][drag-drop]")
{
    const auto paths = LWS::internal::parseUriList(
        "https://example.com/image.png\nfile://server/share/image.png\nfile:///tmp/bad%2.png\n"
        "file:///tmp/bad%00.png\nfile:///tmp/good.png");

    REQUIRE(paths == std::vector<std::filesystem::path>{"/tmp/good.png"});
}
#endif

// ---------------------------------------------------------------------------
// BitmapBuffer default
// ---------------------------------------------------------------------------
TEST_CASE("BitmapBuffer defaults are zeroed", "[bitmap]")
{
    LWS::BitmapBuffer buf;
    REQUIRE(buf.width == 0);
    REQUIRE(buf.height == 0);
    REQUIRE(buf.format == LWS::BitmapPixelFormat::Bgra8Premultiplied);
    REQUIRE(buf.rowOrder == LWS::BitmapRowOrder::TopDown);
    REQUIRE(buf.pixels.empty());
}

TEST_CASE("Bitmap rejects a pixel span smaller than its declared layout", "[bitmap]")
{
    const std::array<std::byte, 4> pixels{};
    REQUIRE_THROWS_AS(LWS::Bitmap({
                          .pixels = pixels,
                          .format = LWS::BitmapPixelFormat::Bgra8,
                          .width = 2,
                          .height = 2,
                          .rowPitch = 8,
                      }),
                      LLUtils::Exception);
}

TEST_CASE("Bitmap normalizes bottom-up straight alpha pixels", "[bitmap]")
{
    const std::array pixels{
        std::byte{0},   std::byte{0}, std::byte{255}, std::byte{128},
        std::byte{255}, std::byte{0}, std::byte{0},   std::byte{255},
    };
    const LWS::Bitmap bitmap({
        .pixels = pixels,
        .format = LWS::BitmapPixelFormat::Bgra8,
        .rowOrder = LWS::BitmapRowOrder::BottomUp,
        .width = 1,
        .height = 2,
        .rowPitch = 4,
    });

    const auto normalized = bitmap.GetBuffer();
    REQUIRE(normalized.format == LWS::BitmapPixelFormat::Bgra8Premultiplied);
    REQUIRE(normalized.rowOrder == LWS::BitmapRowOrder::TopDown);
    REQUIRE(normalized.pixels[0] == std::byte{255});
    REQUIRE(normalized.pixels[6] == std::byte{128});
}

TEST_CASE("Bitmap resize preserves aspect ratio and centers content", "[bitmap]")
{
    std::array<std::byte, 4U * 2U * 4U> pixels{};
    for (size_t offset = 0; offset < pixels.size(); offset += 4U)
    {
        pixels[offset + 2] = std::byte{255};
        pixels[offset + 3] = std::byte{255};
    }
    const LWS::Bitmap bitmap({
        .pixels = pixels,
        .format = LWS::BitmapPixelFormat::Bgra8Premultiplied,
        .rowOrder = LWS::BitmapRowOrder::TopDown,
        .width = 4,
        .height = 2,
        .rowPitch = 16,
    });

    const auto resizedBitmap = bitmap.resize(4, 4, {255, 255, 255, 255});
    const auto resized = resizedBitmap->GetBuffer();
    REQUIRE(resized.pixels[0] == std::byte{255});
    REQUIRE(resized.pixels[1] == std::byte{255});
    REQUIRE(resized.pixels[2] == std::byte{255});
    REQUIRE(resized.pixels[static_cast<size_t>(resized.rowPitch) + 2] == std::byte{255});
    REQUIRE(resized.pixels[static_cast<size_t>(resized.rowPitch) + 1] == std::byte{0});
}

TEST_CASE("Bitmap resize composites transparency over its background", "[bitmap]")
{
    const std::array pixels{std::byte{0}, std::byte{0}, std::byte{255}, std::byte{128}};
    const LWS::Bitmap bitmap({
        .pixels = pixels,
        .format = LWS::BitmapPixelFormat::Bgra8,
        .rowOrder = LWS::BitmapRowOrder::TopDown,
        .width = 1,
        .height = 1,
        .rowPitch = 4,
    });

    const auto resizedBitmap = bitmap.resize(1, 1, {255, 255, 255, 255});
    const auto resized = resizedBitmap->GetBuffer();
    REQUIRE(resized.pixels[0] == std::byte{127});
    REQUIRE(resized.pixels[1] == std::byte{127});
    REQUIRE(resized.pixels[2] == std::byte{255});
    REQUIRE(resized.pixels[3] == std::byte{255});
}

TEST_CASE("Notification icon rectangles use signed screen coordinates", "[notification-icon]")
{
    using IconRect = decltype(std::declval<const LWS::NotificationIconGroup&>().GetIconRect(0));
    REQUIRE(std::is_signed_v<typename IconRect::Point_Type::point_type>);
}

TEST_CASE("Listeners retain their captures during self-disconnection", "[event][lifetime]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState listeners(platform);
    auto capture = std::make_shared<int>(42);
    const std::weak_ptr<int> lifetime = capture;
    uint64_t id{};
    bool aliveDuringCallback{};
    id = listeners.Add(
        [&, capture = std::move(capture)](const LWS::AnyEvent&)
        {
            listeners.Remove(id);
            aliveDuringCallback = !lifetime.expired();
            return LWS::EventResponse::Unhandled;
        });

    REQUIRE(listeners.Dispatch(LWS::EventPaint{}) == LWS::EventResponse::Unhandled);
    REQUIRE(aliveDuringCallback);
    REQUIRE(lifetime.expired());
    REQUIRE_FALSE(listeners.Contains(id));
}

TEST_CASE("Listener mutations preserve dispatch order and defer new registrations", "[event][lifetime]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState listeners(platform);
    std::vector<int> calls;
    uint64_t first{};
    uint64_t removed{};
    first = listeners.Add(
        [&](const LWS::AnyEvent&)
        {
            calls.push_back(1);
            listeners.Remove(first);
            listeners.Remove(removed);
            std::ignore = listeners.Add(
                [&](const LWS::AnyEvent&)
                {
                    calls.push_back(3);
                    return LWS::EventResponse::Unhandled;
                });
            return LWS::EventResponse::Unhandled;
        });
    removed = listeners.Add(
        [&](const LWS::AnyEvent&)
        {
            calls.push_back(2);
            return LWS::EventResponse::Unhandled;
        });

    REQUIRE(listeners.Dispatch(LWS::EventPaint{}) == LWS::EventResponse::Unhandled);
    REQUIRE(calls == std::vector{1});
    REQUIRE(listeners.Dispatch(LWS::EventPaint{}) == LWS::EventResponse::Unhandled);
    REQUIRE(calls == std::vector{1, 3});
}

TEST_CASE("Closing listeners preserves active captures and stops propagation", "[event][lifetime]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState listeners(platform);
    auto capture = std::make_shared<int>(42);
    const std::weak_ptr<int> lifetime = capture;
    bool aliveDuringCallback{};
    bool laterCalled{};
    std::ignore = listeners.Add(
        [&, capture = std::move(capture)](const LWS::AnyEvent&)
        {
            listeners.Close();
            aliveDuringCallback = !lifetime.expired();
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = listeners.Add(
        [&](const LWS::AnyEvent&)
        {
            laterCalled = true;
            return LWS::EventResponse::Unhandled;
        });

    REQUIRE(listeners.Dispatch(LWS::EventPaint{}) == LWS::EventResponse::Unhandled);
    REQUIRE(aliveDuringCallback);
    REQUIRE(lifetime.expired());
    REQUIRE_FALSE(laterCalled);
    REQUIRE(listeners.IsClosed());
}

#ifdef LWS_PLATFORM_WIN32
TEST_CASE("Typed listeners retain their captures during self-disconnection", "[event][lifetime][win32]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState listeners(platform);
    auto capture = std::make_shared<int>(42);
    const std::weak_ptr<int> lifetime = capture;
    uint64_t id{};
    bool aliveDuringCallback{};
    id = listeners.AddPlatform(
        [&, capture = std::move(capture)](const LWS::Win32::PlatformEvent&) -> std::optional<LRESULT>
        {
            listeners.Remove(id);
            aliveDuringCallback = !lifetime.expired();
            return 7;
        });

    LRESULT result{};
    REQUIRE(listeners.DispatchPlatform(LWS::Win32::ActivationEvent{true}, result));
    REQUIRE(result == 7);
    REQUIRE(aliveDuringCallback);
    REQUIRE(lifetime.expired());
    REQUIRE_FALSE(listeners.Contains(id));
}
#endif
