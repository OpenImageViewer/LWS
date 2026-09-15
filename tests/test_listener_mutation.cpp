#include <catch2/catch_test_macros.hpp>
#include <LWS/Platform.hpp>
#include <LWS/Timer.hpp>
#include <LWS/Window.hpp>
#include <LWS/source/internal/ListenerState.hpp>
#include <LWS/source/internal/WindowBackendAccess.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
#endif
#include <memory>
#include <vector>

TEST_CASE("Nested dispatch shares one pending list until the outermost event returns", "[event][mutation]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState state(platform);
    bool nesting = false, entered = false;
    uint64_t d{}, e{};
    unsigned dCalls = 0, eCalls = 0;
    std::ignore = state.Add(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event) && !entered)
            {
                entered = true;
                d = state.Add(
                    [&](const auto&)
                    {
                        ++dCalls;
                        return LWS::EventResponse::Unhandled;
                    });
                nesting = true;
                std::ignore = state.Dispatch(LWS::EventFocusGained{});
                nesting = false;
            }
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Add(
        [&](const LWS::AnyEvent&)
        {
            if (nesting && !e)
                e = state.Add(
                    [&](const auto&)
                    {
                        ++eCalls;
                        return LWS::EventResponse::Unhandled;
                    });
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(state.Contains(d));
    REQUIRE(state.Contains(e));
    REQUIRE(dCalls == 0);
    REQUIRE(eCalls == 0);
    std::ignore = state.Dispatch(LWS::EventFocusLost{});
    REQUIRE(dCalls == 1);
    REQUIRE(eCalls == 1);
}

TEST_CASE("Pending registrations can disconnect and cannot reappear after Close", "[event][mutation]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState state(platform);
    uint64_t pending{};
    bool connected = false, disconnected = false;
    unsigned calls = 0;
    std::ignore = state.Add(
        [&](const LWS::AnyEvent&)
        {
            pending = state.Add(
                [&](const auto&)
                {
                    ++calls;
                    return LWS::EventResponse::Unhandled;
                });
            connected = state.Contains(pending);
            state.Remove(pending);
            disconnected = !state.Contains(pending);
            std::ignore = state.Add(
                [&](const auto&)
                {
                    ++calls;
                    return LWS::EventResponse::Unhandled;
                });
            state.Close();
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(connected);
    REQUIRE(disconnected);
    REQUIRE(state.IsClosed());
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(calls == 0);
}

TEST_CASE("Handled and exceptional returns publish pending registrations", "[event][mutation]")
{
    LWS::PlatformContext platform;
    unsigned errors = 0;
    platform.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++errors; });
    LWS::internal::ListenerState state(platform);
    bool throwException = false;
    SECTION("handled") {}
    SECTION("exception")
    {
        throwException = true;
    }
    bool first = true;
    unsigned calls = 0;
    uint64_t late{};
    std::ignore = state.Add(
        [&](const LWS::AnyEvent&)
        {
            if (first)
            {
                first = false;
                late = state.Add(
                    [&](const auto&)
                    {
                        ++calls;
                        return LWS::EventResponse::Unhandled;
                    });
                if (throwException)
                    throw 42;
                return LWS::EventResponse::Handled;
            }
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(state.Contains(late));
    REQUIRE(calls == 0);
    REQUIRE(errors == (throwException ? 1U : 0U));
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(calls == 1);
}

TEST_CASE("Retired captures can register and dispatch without invalidating publication", "[event][mutation][lifetime]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState state(platform);
    uint64_t late{};
    unsigned calls = 0;
    struct Capture
    {
        LWS::internal::ListenerState& state;
        uint64_t& id;
        unsigned& calls;
        ~Capture()
        {
            id = state.Add(
                [&calls = calls](const auto&)
                {
                    ++calls;
                    return LWS::EventResponse::Unhandled;
                });
            std::ignore = state.Dispatch(LWS::EventFocusGained{});
        }
    };
    auto capture = std::make_shared<Capture>(state, late, calls);
    const std::weak_ptr<Capture> lifetime = capture;
    uint64_t first{};
    first = state.Add(
        [&, capture = std::move(capture)](const LWS::AnyEvent&)
        {
            state.Remove(first);
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(lifetime.expired());
    REQUIRE(state.Contains(late));
    REQUIRE(calls == 0);
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(calls == 1);
}

#ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("Portable and typed events share the deferred publication boundary", "[event][mutation][win32]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState state(platform);
    unsigned calls = 0;
    bool nestedOverride = true;
    std::ignore = state.Add(
        [&](const LWS::AnyEvent&)
        {
            std::ignore = state.AddPlatform(
                [&](const auto&) -> std::optional<LRESULT>
                {
                    ++calls;
                    return 7;
                });
            LRESULT result = 0;
            nestedOverride = state.DispatchPlatform(LWS::Win32::ActivationEvent{true}, result);
            return LWS::EventResponse::Unhandled;
        });
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE_FALSE(nestedOverride);
    REQUIRE(calls == 0);
    LRESULT result = 0;
    REQUIRE(state.DispatchPlatform(LWS::Win32::ActivationEvent{true}, result));
    REQUIRE(result == 7);
    REQUIRE(calls == 1);
}
#endif

#if defined(LWS_HAS_WIN32_BACKEND) || defined(LWS_HAS_WAYLAND_BACKEND)
TEST_CASE("New cleanup listeners and timer targets receive nested destruction", "[event][mutation][lifetime]")
{
    LWS::PlatformContext platform;
    #ifdef LWS_HAS_WIN32_BACKEND
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
    REQUIRE(platform.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    LWS::Window window(platform);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Timer timer(platform);
    LWS::EventConnection cleanup;
    unsigned cleaned = 0;
    auto first = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event))
            {
                auto connection = window.Listen(
                    [&](const LWS::AnyEvent& notification)
                    {
                        if (std::holds_alternative<LWS::EventWindowDestroying>(notification))
                            ++cleaned;
                        return LWS::EventResponse::Unhandled;
                    });
                REQUIRE(connection);
                cleanup = std::move(*connection);
                REQUIRE(cleanup.IsConnected());
                REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
                timer.SetInterval(1);
                REQUIRE(window.Destroy() == LWS::Result::Success);
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(first);
    std::ignore = LWS::internal::WindowBackendAccess::Dispatch(window, LWS::EventPaint{});
    REQUIRE(cleaned == 1);
    REQUIRE_FALSE(cleanup.IsConnected());
    REQUIRE_NOTHROW(timer.SetInterval(2));
    REQUIRE(timer.GetInterval() == 2);
}
#endif

TEST_CASE("Bulk close releases callbacks after detaching both listener lists", "[event][mutation][lifetime]")
{
    LWS::PlatformContext platform;
    LWS::internal::ListenerState state(platform);
    unsigned destroyed = 0, invoked = 0, aliveAtClose = 0;
    struct Capture
    {
        LWS::internal::ListenerState& state;
        unsigned& destroyed;
        ~Capture()
        {
            state.Remove(0);
            state.Close();
            ++destroyed;
        }
    };
    for (unsigned i = 0; i < 100; ++i)
    {
        auto capture = std::make_shared<Capture>(state, destroyed);
        std::ignore = state.Add(
            [&, capture = std::move(capture)](const LWS::AnyEvent&)
            {
                ++invoked;
                std::ignore = state.Add([](const auto&) { return LWS::EventResponse::Unhandled; });
                state.Close();
                aliveAtClose = 100 - destroyed;
                return LWS::EventResponse::Unhandled;
            });
    }
    std::ignore = state.Dispatch(LWS::EventPaint{});
    REQUIRE(invoked == 1);
    REQUIRE(aliveAtClose == 100);
    REQUIRE(destroyed == 100);
    REQUIRE(state.IsClosed());
}
