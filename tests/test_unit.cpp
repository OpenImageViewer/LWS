// Unit tests for LWS types that don't require a real window or platform init.
#include <catch2/catch_test_macros.hpp>

#include <LWS/Event.hpp>
#include <LWS/KeyCode.hpp>
#include <LWS/MouseButton.hpp>
#include <LWS/Result.hpp>
#include <LWS/Window.hpp>
#include <LWS/WindowDisplayState.hpp>
#include <LWS/interfaces/backends.hpp>

// ---------------------------------------------------------------------------
// Result enum
// ---------------------------------------------------------------------------
TEST_CASE("Result enum covers all expected values", "[result]")
{
    REQUIRE(static_cast<int>(LWS::Result::Success)      == 0);
    REQUIRE(LWS::Result::Failure        != LWS::Result::Success);
    REQUIRE(LWS::Result::InvalidState   != LWS::Result::Success);
    REQUIRE(LWS::Result::NotSupported   != LWS::Result::Success);
    REQUIRE(LWS::Result::AlreadyCreated != LWS::Result::Success);
    REQUIRE(LWS::Result::NotCreated     != LWS::Result::Success);
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
    REQUIRE(cfg.displayState == LWS::WindowDisplayState::Restored);
}

// ---------------------------------------------------------------------------
// AnyEvent variant dispatch
// ---------------------------------------------------------------------------
TEST_CASE("AnyEvent variant holds EventResize and can be visited", "[event]")
{
    LWS::AnyEvent ev = LWS::EventResize{ {1280, 720} };
    bool visited = false;
    std::visit([&](const auto& e) {
        if constexpr (std::is_same_v<std::decay_t<decltype(e)>, LWS::EventResize>)
        {
            REQUIRE(e.newClientSize.x == 1280);
            REQUIRE(e.newClientSize.y == 720);
            visited = true;
        }
    }, ev);
    REQUIRE(visited);
}

TEST_CASE("AnyEvent variant holds EventKeyDown", "[event]")
{
    LWS::AnyEvent ev = LWS::EventKeyDown{ LWS::KeyCode::A, false };
    REQUIRE(std::holds_alternative<LWS::EventKeyDown>(ev));
    REQUIRE(std::get<LWS::EventKeyDown>(ev).key == LWS::KeyCode::A);
    REQUIRE(std::get<LWS::EventKeyDown>(ev).repeat == false);
}

TEST_CASE("AnyEvent variant holds EventMouseButton", "[event]")
{
    LWS::AnyEvent ev = LWS::EventMouseButton{ LWS::MouseButton::Left, true, {100, 200} };
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
    REQUIRE(LWS::WindowDisplayState::Restored  != LWS::WindowDisplayState::Minimized);
    REQUIRE(LWS::WindowDisplayState::Minimized != LWS::WindowDisplayState::Maximized);
    REQUIRE(LWS::WindowDisplayState::Restored  != LWS::WindowDisplayState::Maximized);
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

// ---------------------------------------------------------------------------
// BitmapBuffer default
// ---------------------------------------------------------------------------
TEST_CASE("BitmapBuffer defaults are zeroed", "[bitmap]")
{
    LWS::BitmapBuffer buf;
    REQUIRE(buf.width == 0);
    REQUIRE(buf.height == 0);
    REQUIRE(buf.bitsPerPixel == 32);
    REQUIRE(buf.pixels.empty());
}
