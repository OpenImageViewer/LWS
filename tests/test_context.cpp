#include <catch2/catch_test_macros.hpp>

#include <LWS/Clipboard.hpp>
#include <LWS/Platform.hpp>
#include <LWS/Timer.hpp>
#include <LWS/Window.hpp>


#include <array>
#include <chrono>
#include <memory>
#include <thread>

#if defined(LWS_HAS_WIN32_BACKEND) || defined(LWS_HAS_WAYLAND_BACKEND)

namespace
{
    void Initialize(LWS::PlatformContext& context)
    {
#ifdef LWS_HAS_WIN32_BACKEND
        REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
#else
        REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
#endif
    }
}

TEST_CASE("Context shutdown cannot destroy an active message dispatcher", "[platform][lifetime]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Result nested = LWS::Result::Failure;
    REQUIRE(context.PostTask([&]
                            {
                                nested = context.Shutdown();
                                context.RequestQuit();
                            }) == LWS::Result::Success);
    SECTION("Nonblocking dispatch")
    {
        std::ignore = context.ProcessMessages();
    }
    SECTION("Message loop")
    {
        context.RunMessageLoop();
    }
    REQUIRE(nested == LWS::Result::InvalidState);
    REQUIRE(context.IsUsable());
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Context shutdown rejects recursion while draining accepted tasks", "[platform][lifetime]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Result nested = LWS::Result::Failure;
    bool laterRan = false;
    REQUIRE(context.PostTask([&] { nested = context.Shutdown(); }) == LWS::Result::Success);
    REQUIRE(context.PostTask([&] { laterRan = true; }) == LWS::Result::Success);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(nested == LWS::Result::InvalidState);
    REQUIRE(laterRan);
    REQUIRE_FALSE(context.IsUsable());
}

TEST_CASE("A service created by a shutdown task keeps the context active", "[platform][lifetime]")
{
    LWS::PlatformContext context;
    Initialize(context);
    std::unique_ptr<LWS::Clipboard> clipboard;
    REQUIRE(context.PostTask([&] { clipboard = std::make_unique<LWS::Clipboard>(context); }) ==
            LWS::Result::Success);
    REQUIRE(context.Shutdown() == LWS::Result::InvalidState);
    REQUIRE(context.IsUsable());
    REQUIRE(clipboard != nullptr);
    clipboard.reset();
    bool ran = false;
    REQUIRE(context.PostTask([&] { ran = true; }) == LWS::Result::Success);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(ran);
}

TEST_CASE("Empty timer callbacks remain disabled", "[timer][callback]")
{
    LWS::PlatformContext context;
    Initialize(context);
    unsigned exceptions = 0;
    context.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Timer timer(context);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    unsigned calls = 0;
    timer.SetCallback([&] { ++calls; });
    timer.SetInterval(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (calls == 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(calls != 0);
    timer.SetCallback({});
    const unsigned previousCalls = calls;
    LWS::HighPrecisionTimer precise(context, {});
    precise.SetDueTime(0);
    precise.Enable(true);
    const auto quietDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(50);
    while (std::chrono::steady_clock::now() < quietDeadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(calls == previousCalls);
    REQUIRE(exceptions == 0);
}

TEST_CASE("Window destruction tolerates child callbacks removing siblings", "[window][lifetime]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window parent(context);
    LWS::Window first(context);
    auto second = std::make_unique<LWS::Window>(context);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(first.Create({.parent = &parent}) == LWS::Result::Success);
    REQUIRE(second->Create({.parent = &parent}) == LWS::Result::Success);
    LWS::Result recursive = LWS::Result::Failure;
    unsigned destroyed = 0;
    auto listener = first.Listen([&](const LWS::AnyEvent& event)
                                 {
                                     if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                                     {
                                         ++destroyed;
                                         recursive = first.Destroy();
                                         second.reset();
                                     }
                                     return LWS::EventResponse::Unhandled;
                                 });
    REQUIRE(listener.has_value());
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE(destroyed == 1);
    REQUIRE(recursive == LWS::Result::InvalidState);
    REQUIRE(second == nullptr);
    REQUIRE_FALSE(first.IsCreated());
}

TEST_CASE("Client size constraints reject inverted bounds before native requests", "[window][size]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create({.minClientSize = {100, 50}, .maxClientSize = {99, 200}}) ==
            LWS::Result::InvalidArgument);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMinMaxClientSize({100, 50}, {99, 200}) == LWS::Result::InvalidArgument);
    REQUIRE(window.SetMinMaxClientSize({100, 50}, {200, 49}) == LWS::Result::InvalidArgument);
    REQUIRE(window.SetMinMaxClientSize({100, 50}, {0, 200}) == LWS::Result::Success);
}

TEST_CASE("Exception handlers may replace themselves during dispatch", "[platform][callback]")
{
    LWS::PlatformContext context;
    Initialize(context);
    auto capture = std::make_shared<int>(7);
    const std::weak_ptr<int> lifetime = capture;
    bool aliveAfterReset = false;
    context.SetUnhandledExceptionHandler(
        [&, capture = std::move(capture)](std::exception_ptr) noexcept
        {
            context.SetUnhandledExceptionHandler({});
            aliveAfterReset = !lifetime.expired();
        });
    REQUIRE(context.PostTask([] { throw 7; }) == LWS::Result::Success);
    std::ignore = context.ProcessMessages();
    REQUIRE(aliveAfterReset);
    REQUIRE(lifetime.expired());
}

TEST_CASE("Timer targets detach before handled destruction notifications", "[timer][lifetime]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto listener = window.Listen([](const LWS::AnyEvent& event)
                                  {
                                      return std::holds_alternative<LWS::EventWindowDestroyed>(event)
                                                 ? LWS::EventResponse::Handled
                                                 : LWS::EventResponse::Unhandled;
                                  });
    REQUIRE(listener.has_value());
    LWS::Timer timer(context);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    unsigned calls = 0;
    timer.SetCallback([&] { ++calls; });
    timer.SetInterval(1);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    timer.SetInterval(2);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        std::ignore = context.ProcessMessages();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(calls == 0);
    REQUIRE(timer.GetInterval() == 2);
}

TEST_CASE("Moved-from cursor and icon values are rejected", "[window][resource]")
{
    LWS::PlatformContext context;
    Initialize(context);
    LWS::Window window(context);
    auto cursor = LWS::Cursor::FromShape(LWS::CursorShape::Arrow);
    const auto retainedCursor = std::move(cursor);
    REQUIRE(window.SetMouseCursor(cursor) == LWS::Result::InvalidArgument);
    const std::array<std::byte, 4> pixels{};
    auto icon = LWS::WindowIcon::FromBitmap({.pixels = pixels, .width = 1, .height = 1, .rowPitch = 4});
    REQUIRE(icon.has_value());
    const auto retainedIcon = std::move(*icon);
    REQUIRE(window.SetWindowIcon(*icon) == LWS::Result::InvalidArgument);
}

#endif
