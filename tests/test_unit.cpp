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
#include <LWS/WindowDisplayState.hpp>
#include <LWS/interfaces/backends.hpp>
#include <LLUtils/Exception.h>

#include "LWS/source/Wayland/internal/WheelDeltaFrame.hpp"

#include <array>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace
{
    class LifetimeCursorBackend final : public LWS::ICursorBackend
    {
      public:

        explicit LifetimeCursorBackend(bool& destroyed, LWS::Result customCursorResult = LWS::Result::Success)
            : fDestroyed(destroyed), fCustomCursorResult(customCursorResult)
        {
        }
        ~LifetimeCursorBackend() override { fDestroyed = true; }

        void setVisible(bool) override {}
        void setCursorShape(LWS::CursorShape) override {}
        [[nodiscard]] LWS::Result setCustomCursor(const LWS::BitmapBuffer&) override { return fCustomCursorResult; }
        LWS::BackendId backend() const override { return LWS::BackendId::Undefined; }

      private:

        bool& fDestroyed;
        LWS::Result fCustomCursorResult;
    };
}  // namespace

// ---------------------------------------------------------------------------
// Result enum
// ---------------------------------------------------------------------------
TEST_CASE("Result enum covers all expected values", "[result]")
{
    REQUIRE(static_cast<int>(LWS::Result::Success) == 0);
    REQUIRE(LWS::Result::Failure != LWS::Result::Success);
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
    LWS::WindowConfig cfg;
    REQUIRE(cfg.size.x == 800);
    REQUIRE(cfg.size.y == 600);
    REQUIRE(cfg.visible == false);
    REQUIRE(cfg.eraseBackground == true);
    REQUIRE(cfg.alwaysOnTop == false);
    REQUIRE(cfg.transparent == false);
    REQUIRE(cfg.minSize.x == 0);
    REQUIRE(cfg.minSize.y == 0);
    REQUIRE(cfg.maxSize.x == 0);
    REQUIRE(cfg.maxSize.y == 0);
    REQUIRE(cfg.styles == LWS::WindowStyle::NoStyle);
    REQUIRE(cfg.displayState == LWS::WindowDisplayState::Restored);
}

TEST_CASE("Window cursor backend remains alive while a window retains it", "[cursor][lifetime]")
{
    bool destroyed = false;
    std::shared_ptr<LWS::ICursorBackend> retainedBackend;
    {
        LWS::Cursor cursor(std::make_unique<LifetimeCursorBackend>(destroyed));
        retainedBackend = cursor.getBackendShared();
    }

    REQUIRE_FALSE(destroyed);
    retainedBackend.reset();
    REQUIRE(destroyed);
}

TEST_CASE("Cursor forwards custom bitmap failures", "[cursor][bitmap]")
{
    bool destroyed = false;
    LWS::Cursor cursor(std::make_unique<LifetimeCursorBackend>(destroyed, LWS::Result::Failure));
    REQUIRE(cursor.setCustomCursor({}) == LWS::Result::Failure);
}

// ---------------------------------------------------------------------------
// AnyEvent variant dispatch
// ---------------------------------------------------------------------------
TEST_CASE("AnyEvent variant holds EventResize and can be visited", "[event]")
{
    LWS::AnyEvent ev = LWS::EventResize{{1280, 720}};
    bool visited = false;
    std::visit(
        [&](const auto& e)
        {
            if constexpr (std::is_same_v<std::decay_t<decltype(e)>, LWS::EventResize>)
            {
                REQUIRE(e.newClientSize.x == 1280);
                REQUIRE(e.newClientSize.y == 720);
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
// EventListenerToken — type guarantees
// ---------------------------------------------------------------------------
TEST_CASE("EventListenerToken is uint64_t", "[event]")
{
    static_assert(std::is_same_v<LWS::EventListenerToken, uint64_t>);
    LWS::EventListenerToken tok = 42;
    REQUIRE(tok == 42);
}

// ---------------------------------------------------------------------------
// EventListenerGuard construction and move
// ---------------------------------------------------------------------------
TEST_CASE("EventListenerGuard default-constructs safely", "[event][guard]")
{
    // Default guard has null window; destructor must not crash.
    LWS::EventListenerGuard g;
    REQUIRE(g.window == nullptr);
    REQUIRE(g.token == 0);
}

TEST_CASE("EventListenerGuard move constructor transfers ownership", "[event][guard]")
{
    // We need a real Window to construct a non-null guard, but we can still test
    // the move semantics on a default-constructed guard without creating a window.
    LWS::EventListenerGuard g1;
    g1.token = 99;
    LWS::EventListenerGuard g2 = std::move(g1);
    REQUIRE(g2.token == 99);
    REQUIRE(g1.window == nullptr);  // source is nulled after move
}

// ---------------------------------------------------------------------------
// WindowDisplayState enum
// ---------------------------------------------------------------------------
TEST_CASE("WindowDisplayState enum values are distinct", "[state]")
{
    REQUIRE(LWS::WindowDisplayState::Restored != LWS::WindowDisplayState::Minimized);
    REQUIRE(LWS::WindowDisplayState::Minimized != LWS::WindowDisplayState::Maximized);
    REQUIRE(LWS::WindowDisplayState::Restored != LWS::WindowDisplayState::Maximized);
}

// ---------------------------------------------------------------------------
// WindowPlacement struct
// ---------------------------------------------------------------------------
TEST_CASE("WindowPlacement defaults to Restored", "[placement]")
{
    LWS::WindowPlacement p;
    REQUIRE(p.displayState == LWS::WindowDisplayState::Restored);
    REQUIRE(p.position.x == 0);
    REQUIRE(p.position.y == 0);
}

// ---------------------------------------------------------------------------
// BackendId enum
// ---------------------------------------------------------------------------
TEST_CASE("BackendId has expected Win32 value", "[backend]")
{
    REQUIRE(LWS::BackendId::Win32 != LWS::BackendId::Undefined);
    REQUIRE(LWS::BackendId::Wayland != LWS::BackendId::Win32);
}

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
