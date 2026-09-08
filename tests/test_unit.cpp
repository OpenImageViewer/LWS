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

// ---------------------------------------------------------------------------
// AnyEvent variant dispatch
// ---------------------------------------------------------------------------

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

#ifdef LWS_PLATFORM_WIN32

#endif
