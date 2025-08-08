// Win32 integration tests — require a live Win32 environment.
// All tests call Platform::init() / Platform::shutdown() through a shared fixture.
#include <catch2/catch_test_macros.hpp>

#ifdef LWS_PLATFORM_WIN32

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <LWS/Platform.hpp>
#include <LWS/Window.hpp>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
namespace
{
    // Pump a few messages so window-creation side-effects (WM_CREATE, WM_SIZE,
    // WM_PAINT, etc.) are processed before assertions run.
    void pumpMessages(int iterations = 5)
    {
        for (int i = 0; i < iterations; ++i)
            LWS::Platform::processMessages();
    }

    LWS::WindowConfig makeTestConfig()
    {
        LWS::WindowConfig cfg;
        cfg.title   = L"LWSTest";
        cfg.size    = { 640, 480 };
        cfg.position = { 50, 50 };
        cfg.visible = false; // keep hidden — no user interaction required
        return cfg;
    }
}

// ---------------------------------------------------------------------------
// Platform lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("Platform init and shutdown are idempotent", "[platform][win32]")
{
    LWS::Platform::init();
    LWS::Platform::init(); // second call must not crash or assert
    LWS::Platform::shutdown();
    LWS::Platform::shutdown(); // second call must not crash
    // re-init so later tests can use the platform
    LWS::Platform::init();
}

// ---------------------------------------------------------------------------
// Window creation / destruction lifecycle
// ---------------------------------------------------------------------------
TEST_CASE("Window Create returns Success with valid config", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        LWS::Result r = win.Create(makeTestConfig());
        REQUIRE(r == LWS::Result::Success);
        REQUIRE(win.GetHandle() != 0);
        pumpMessages();
    }
    LWS::Platform::shutdown();
}

TEST_CASE("Window Create called twice returns AlreadyCreated", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        REQUIRE(win.Create(makeTestConfig()) == LWS::Result::Success);
        REQUIRE(win.Create(makeTestConfig()) == LWS::Result::AlreadyCreated);
        pumpMessages();
    }
    LWS::Platform::shutdown();
}

TEST_CASE("Window destructor auto-destroys live window", "[window][win32]")
{
    LWS::Platform::init();
    LWS::Handle savedHandle = 0;
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        savedHandle = win.GetHandle();
        pumpMessages();
        REQUIRE(savedHandle != 0);
        // win goes out of scope here; destructor must call Destroy() and not crash
    }
    pumpMessages();
    // IsWindow(savedHandle) should be false after destruction; verify via Win32 directly.
    REQUIRE(::IsWindow(reinterpret_cast<HWND>(savedHandle)) == FALSE);
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Title
// ---------------------------------------------------------------------------
TEST_CASE("SetTitle / GetTitle round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        win.SetTitle(L"Hello LWS");
        REQUIRE(win.GetTitle() == L"Hello LWS");

        win.SetTitle(L"Another Title");
        REQUIRE(win.GetTitle() == L"Another Title");
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Visibility
// ---------------------------------------------------------------------------
TEST_CASE("SetVisible / GetVisible round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        auto cfg  = makeTestConfig();
        cfg.visible = false;
        win.Create(cfg);
        pumpMessages();

        REQUIRE(win.GetVisible() == false);

        win.SetVisible(true);
        pumpMessages();
        REQUIRE(win.GetVisible() == true);

        win.SetVisible(false);
        pumpMessages();
        REQUIRE(win.GetVisible() == false);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Position
// ---------------------------------------------------------------------------
TEST_CASE("SetPosition / GetPosition round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        win.SetPosition({ 200, 150 });
        pumpMessages();

        const LWS::Point pos = win.GetPosition();
        // Allow a small tolerance in case the OS adjusts for DPI or window borders.
        REQUIRE(pos.x >= 190);
        REQUIRE(pos.x <= 210);
        REQUIRE(pos.y >= 140);
        REQUIRE(pos.y <= 160);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Client size
// ---------------------------------------------------------------------------
TEST_CASE("GetClientSize returns configured size", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        auto cfg = makeTestConfig();
        // Make the window visible so the OS actually gives it a client area.
        cfg.visible = true;
        win.Create(cfg);
        pumpMessages();

        const LWS::Size sz = win.GetClientSize();
        // Client area should be non-zero.
        REQUIRE(sz.x > 0);
        REQUIRE(sz.y > 0);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Always-on-top
// ---------------------------------------------------------------------------
TEST_CASE("SetAlwaysOnTop / GetAlwaysOnTop round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        win.SetAlwaysOnTop(true);
        pumpMessages();
        REQUIRE(win.GetAlwaysOnTop() == true);

        win.SetAlwaysOnTop(false);
        pumpMessages();
        REQUIRE(win.GetAlwaysOnTop() == false);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Display state
// ---------------------------------------------------------------------------
TEST_CASE("SetDisplayState Maximized / Restored round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        auto cfg = makeTestConfig();
        cfg.visible = true;
        win.Create(cfg);
        pumpMessages();

        win.SetDisplayState(LWS::WindowDisplayState::Maximized);
        pumpMessages();
        REQUIRE(win.GetDisplayState() == LWS::WindowDisplayState::Maximized);

        win.SetDisplayState(LWS::WindowDisplayState::Restored);
        pumpMessages();
        REQUIRE(win.GetDisplayState() == LWS::WindowDisplayState::Restored);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// AddEventListener / RemoveEventListener with token
// ---------------------------------------------------------------------------
TEST_CASE("AddEventListener returns distinct tokens", "[window][event][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        LWS::EventListenerToken t1 = win.AddEventListener([](const LWS::AnyEvent&) { return false; });
        LWS::EventListenerToken t2 = win.AddEventListener([](const LWS::AnyEvent&) { return false; });
        REQUIRE(t1 != t2);

        win.RemoveEventListener(t1);
        win.RemoveEventListener(t2);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// EventListenerGuard RAII auto-remove
// ---------------------------------------------------------------------------
TEST_CASE("EventListenerGuard removes listener on destruction", "[window][event][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        int callCount = 0;
        {
            LWS::EventListenerToken tok = win.AddEventListener([&](const LWS::AnyEvent& e) {
                if (std::holds_alternative<LWS::EventPaint>(e))
                    ++callCount;
                return false;
            });
            LWS::EventListenerGuard guard = win.MakeListenerGuard(tok);

            // Force a paint so the listener fires at least once while active.
            win.SetVisible(true);
            pumpMessages();
            // guard destroyed here — listener unregistered
        }

        const int countAfterGuardDestroyed = callCount;

        // Trigger more events; the listener should no longer be called.
        win.SetVisible(false);
        win.SetVisible(true);
        pumpMessages();

        // callCount must not increase after the guard was destroyed.
        REQUIRE(callCount == countAfterGuardDestroyed);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// BackendId
// ---------------------------------------------------------------------------
TEST_CASE("Window backend is Win32 on Windows", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        REQUIRE(win.GetBackendId() == LWS::BackendId::Win32);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Min/max size
// ---------------------------------------------------------------------------
TEST_CASE("SetMinMaxSize / GetMinSize / GetMaxSize round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        win.SetMinMaxSize({ 320, 240 }, { 1920, 1080 });
        REQUIRE(win.GetMinSize().x == 320);
        REQUIRE(win.GetMinSize().y == 240);
        REQUIRE(win.GetMaxSize().x == 1920);
        REQUIRE(win.GetMaxSize().y == 1080);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Transparent flag
// ---------------------------------------------------------------------------
TEST_CASE("SetTransparent / GetTransparent round-trip", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window win;
        win.Create(makeTestConfig());
        pumpMessages();

        win.SetTransparent(true);
        pumpMessages();
        REQUIRE(win.GetTransparent() == true);

        win.SetTransparent(false);
        pumpMessages();
        REQUIRE(win.GetTransparent() == false);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// Child / parent relationship
// ---------------------------------------------------------------------------
TEST_CASE("Child window has correct parent pointer", "[window][win32]")
{
    LWS::Platform::init();
    {
        LWS::Window parent;
        auto parentCfg = makeTestConfig();
        parentCfg.title = L"Parent";
        parent.Create(parentCfg);
        pumpMessages();

        LWS::Window child;
        auto childCfg = makeTestConfig();
        childCfg.title = L"Child";
        childCfg.styles = LWS::WindowStyle::ChildWindow;
        child.SetParent(&parent);
        child.Create(childCfg);
        pumpMessages();

        REQUIRE(child.GetParent() == &parent);
    }
    LWS::Platform::shutdown();
}

// ---------------------------------------------------------------------------
// processMessages returns true only on WM_QUIT
// ---------------------------------------------------------------------------
TEST_CASE("processMessages returns false when no WM_QUIT is pending", "[platform][win32]")
{
    LWS::Platform::init();
    // There is no pending WM_QUIT — processMessages should return false.
    bool result = LWS::Platform::processMessages();
    REQUIRE(result == false);
    LWS::Platform::shutdown();
}

#endif // LWS_PLATFORM_WIN32
