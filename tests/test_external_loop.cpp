#include <catch2/catch_test_macros.hpp>

#ifdef LWS_HAS_WIN32_BACKEND
    #include "Win32TestSupport.hpp"
    #include <LWS/Timer.hpp>
    #include <catch2/generators/catch_generators.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <Windows.h>

    #include <array>
    #include <thread>
    #include <vector>

using LwsTest::DispatchHostMessages;

TEST_CASE("External message loop tasks run through the host dispatcher and preserve queue order",
          "[platform][external-loop][win32]")
{
    LwsTest::Context initialized(LWS::DispatchMode::ExternalLoop);
    auto& context = initialized.value;
    const auto uiThread = std::this_thread::get_id();
    bool correctThread = true;
    std::vector<unsigned> order;
    std::array<LWS::Result, 256> results{};
    std::thread worker(
        [&]
        {
            for (unsigned index = 0; index < results.size(); ++index)
                results[index] = context.PostTask(
                    [&, index]
                    {
                        correctThread &= std::this_thread::get_id() == uiThread;
                        order.push_back(index);
                        if (index == 0)
                            REQUIRE(context.PostTask([&] { order.push_back(256); }) == LWS::Result::Success);
                    });
        });
    worker.join();
    for (const auto result : results)
        REQUIRE(result == LWS::Result::Success);
    REQUIRE(order.empty());
    DispatchHostMessages();
    REQUIRE(correctThread);
    REQUIRE(order.size() == 257);
    for (unsigned index = 0; index < order.size(); ++index)
        REQUIRE(order[index] == index);
    DispatchHostMessages();
    REQUIRE(order.size() == 257);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("External message loop callbacks protect context lifetime without quitting the host",
          "[platform][external-loop][win32]")
{
    LwsTest::Context initialized(LWS::DispatchMode::ExternalLoop);
    auto& context = initialized.value;
    LWS::Result shutdown = LWS::Result::Success;
    unsigned exceptions = 0;
    bool laterRan = false;
    context.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    REQUIRE(context.PostTask([] { throw 7; }) == LWS::Result::Success);
    REQUIRE(context.PostTask(
                [&]
                {
                    shutdown = context.Shutdown();
                    context.RequestQuit();
                    laterRan = true;
                }) == LWS::Result::Success);
    DispatchHostMessages();
    REQUIRE(exceptions == 1);
    REQUIRE(laterRan);
    REQUIRE(shutdown == LWS::Result::InvalidState);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    DispatchHostMessages();
}

TEST_CASE("External message loop shutdown drains tasks and unregisters the native dispatcher",
          "[platform][external-loop][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    for (unsigned iteration = 0; iteration < 3; ++iteration)
    {
        LWS::PlatformContext context;
        REQUIRE(context.Init({.backend = LWS::BackendId::Win32, .dispatchMode = LWS::DispatchMode::ExternalLoop}) ==
                LWS::Result::Success);
        DispatchHostMessages();
        unsigned calls = 0;
        REQUIRE(context.PostTask([&] { ++calls; }) == LWS::Result::Success);
        MSG wake{};
        REQUIRE(PeekMessageW(&wake, nullptr, WM_APP, 0xBFFF, PM_NOREMOVE));
        REQUIRE(wake.hwnd != nullptr);
        wchar_t className[64]{};
        REQUIRE(GetClassNameW(wake.hwnd, className, static_cast<int>(std::size(className))) != 0);
        const auto module = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(wake.hwnd, GWLP_HINSTANCE));
        REQUIRE(context.Shutdown() == LWS::Result::Success);
        REQUIRE(calls == 1);
        REQUIRE_FALSE(IsWindow(wake.hwnd));
        WNDCLASSW windowClass{};
        REQUIRE_FALSE(GetClassInfoW(module, className, &windowClass));
        REQUIRE(context.PostTask([] {}) == LWS::Result::InvalidState);
        DispatchHostMessages();
        REQUIRE(calls == 1);
    }
}
TEST_CASE("External message loop cleanup preserves an existing host quit request", "[platform][external-loop][win32]")
{
    LwsTest::Context initialized(LWS::DispatchMode::ExternalLoop);
    auto& context = initialized.value;
    REQUIRE(context.PostTask([] {}) == LWS::Result::Success);
    constexpr int exitCode = 37;
    constexpr UINT hostMessage = WM_APP + 17;
    REQUIRE(PostThreadMessageW(GetCurrentThreadId(), hostMessage, 0, 0));
    PostQuitMessage(exitCode);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    // WM_QUIT is synthesized at low priority. Consume earlier posted messages as the host loop would,
    // rather than assuming a WM_QUIT-only filter can retrieve it while other host messages are pending.
    MSG message{};
    bool foundQuit = false;
    bool foundHostMessage = false;
    while (!foundQuit && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        foundQuit = message.message == WM_QUIT;
        foundHostMessage |= message.message == hostMessage;
        if (!foundQuit)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    REQUIRE(foundHostMessage);
    REQUIRE(foundQuit);
    REQUIRE(message.message == WM_QUIT);
    REQUIRE(message.wParam == exitCode);
}
TEST_CASE("High precision timer cleanup preserves the host quit code", "[platform][external-loop][timer][win32]")
{
    const bool destroy = GENERATE(false, true);
    LwsTest::Context initialized(LWS::DispatchMode::ExternalLoop);
    auto timer = std::make_unique<LWS::HighPrecisionTimer>(initialized.value, [] {});
    timer->SetDueTime(10000);
    timer->Enable(true);
    constexpr int exitCode = 53;
    PostQuitMessage(exitCode);
    if (destroy)
        timer.reset();
    else
        timer->Enable(false);
    // Earlier window tests can leave native notifications ahead of the synthesized quit message.
    MSG message{};
    bool foundQuit = false;
    while (!foundQuit && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
    {
        foundQuit = message.message == WM_QUIT;
        if (!foundQuit)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    REQUIRE(foundQuit);
    CHECK(message.wParam == exitCode);
}
#endif
