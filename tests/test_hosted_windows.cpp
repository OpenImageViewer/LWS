#include <catch2/catch_test_macros.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Window.hpp>
    #include "Win32TestSupport.hpp"
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <Windows.h>
    #include <LWS/Timer.hpp>
    #include <LWS/Win32/ScopedWindowDpiContext.hpp>
    #include <future>
    #include <latch>
    #include <thread>

TEST_CASE("Foreign parents own native containment but not the LWS context", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    const HWND host = LwsTest::CreateHost(L"WLX host", WS_OVERLAPPEDWINDOW);
    REQUIRE(host != nullptr);
    {
        LWS::Window container(platform);
        REQUIRE(LWS::Win32::CreateChild(container, host, {.position = LWS::Point{0, 0}}) == LWS::Result::Success);
        CHECK(container.GetParent() == nullptr);
        const auto hwnd = *LWS::Win32::GetHwnd(container);
        REQUIRE(GetParent(hwnd) == host);
        REQUIRE((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_CHILD) != 0);
        REQUIRE(container.RequestActivation() == LWS::Result::NotSupported);
        REQUIRE(container.SetWindowMode(LWS::WindowMode::Fullscreen) == LWS::Result::NotSupported);
        unsigned destroying = 0;
        auto connection = container.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (const auto* args = std::get_if<LWS::EventWindowDestroying>(&event))
                {
                    REQUIRE(args->nativeResourcesAvailable);
                    REQUIRE(LWS::Win32::GetHwnd(container).has_value());
                    ++destroying;
                }
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(connection);
        REQUIRE(DestroyWindow(host));
        REQUIRE(destroying == 1);
        REQUIRE_FALSE(container.IsCreated());
        REQUIRE(container.Destroy() == LWS::Result::Success);
        REQUIRE(destroying == 1);
        REQUIRE(platform.ProcessMessages() == LWS::LoopResult::Continue);
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Reparenting preserves child handles and updates LWS teardown ownership", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    {
        LWS::Window dock(platform), shell(platform), composite(platform), canvas(platform);
        REQUIRE(dock.Create() == LWS::Result::Success);
        REQUIRE(shell.Create() == LWS::Result::Success);
        REQUIRE(composite.Create({.parent = &dock}) == LWS::Result::Success);
        REQUIRE(canvas.Create({.parent = &composite}) == LWS::Result::Success);
        const HWND compositeHandle = *LWS::Win32::GetHwnd(composite);
        const HWND canvasHandle = *LWS::Win32::GetHwnd(canvas);
        REQUIRE(composite.Reparent(canvas) == LWS::Result::InvalidArgument);
        REQUIRE(composite.Reparent(shell) == LWS::Result::Success);
        REQUIRE(composite.GetParent() == &shell);
        REQUIRE(GetParent(compositeHandle) == *LWS::Win32::GetHwnd(shell));
        REQUIRE(*LWS::Win32::GetHwnd(canvas) == canvasHandle);
        REQUIRE(dock.Destroy() == LWS::Result::Success);
        REQUIRE(composite.IsCreated());
        REQUIRE(canvas.IsCreated());
        REQUIRE(shell.Destroy() == LWS::Result::Success);
        REQUIRE_FALSE(composite.IsCreated());
        REQUIRE_FALSE(canvas.IsCreated());
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Child wrappers can outlive their parent backend after native teardown", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    {
        LWS::Window child(platform);
        auto parent = std::make_unique<LWS::Window>(platform);
        REQUIRE(parent->Create() == LWS::Result::Success);
        REQUIRE(child.Create({.parent = parent.get()}) == LWS::Result::Success);
        const HWND handle = *LWS::Win32::GetHwnd(child);
        // Parent teardown ends the native child lifetime before releasing its backend. The surviving
        // child wrapper must remain safe to query and destroy without dereferencing its former parent.
        parent.reset();
        CHECK_FALSE(IsWindow(handle));
        CHECK_FALSE(child.IsCreated());
        CHECK(child.GetParent() == nullptr);
        CHECK_FALSE(LWS::Win32::GetHwnd(child));
        CHECK(child.Destroy() == LWS::Result::Success);
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}

TEST_CASE("External child windows own their resources and observe their own geometry", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    const HWND host = LwsTest::CreateHost(L"Untouched host", WS_OVERLAPPEDWINDOW);
    REQUIRE(host);
    {
        LWS::Window first(platform), second(platform), canvas(platform), shell(platform);
        REQUIRE(LWS::Win32::CreateChild(first, host) == LWS::Result::Success);
        REQUIRE(LWS::Win32::CreateChild(second, host) == LWS::Result::Success);
        REQUIRE(canvas.Create({.parent = &first}) == LWS::Result::Success);
        REQUIRE(shell.Create() == LWS::Result::Success);
        CHECK(first.SetTitle(L"Owned child") == LWS::Result::Success);
        CHECK(first.RequestShowState(LWS::WindowShowState::Maximized) == LWS::Result::NotSupported);
        CHECK(first.BeginWindowDrag(LWS::WindowDragOperation::Move) == LWS::Result::InvalidState);
        CHECK(first.Center(LWS::CenterTarget::Parent) == LWS::Result::InvalidState);
        LWS::Timer timer(platform);
        REQUIRE(timer.SetTargetWindow(&first) == LWS::Result::Success);
        unsigned sizes{}, destroyed{};
        auto listener = first.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
                    ++sizes;
                if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                    ++destroyed;
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(listener);
        // The host sizes our owned child. No observation hook on the external parent is required.
        const HWND handle = *LWS::Win32::GetHwnd(first);
        REQUIRE(SetWindowPos(handle, nullptr, 10, 20, 300, 240, SWP_NOACTIVATE | SWP_NOZORDER));
        RECT rect{};
        REQUIRE(GetClientRect(handle, &rect));
        CHECK(first.GetClientAreaMetrics().pixels == LWS::PixelSize{rect.right, rect.bottom});
        CHECK(sizes > 0);
        // Joining a managed parent preserves the native handle and transfers family teardown responsibility.
        REQUIRE(second.Reparent(shell) == LWS::Result::Success);
        CHECK(second.GetParent() == &shell);
        CHECK(GetParent(*LWS::Win32::GetHwnd(second)) == *LWS::Win32::GetHwnd(shell));
        REQUIRE(first.Destroy() == LWS::Result::Success);
        CHECK(destroyed == 1);
        CHECK_FALSE(canvas.IsCreated());
        CHECK(IsWindow(host));
        CHECK(second.IsCreated());
        wchar_t title[32]{};
        REQUIRE(GetWindowTextW(host, title, 32) > 0);
        CHECK(std::wstring(title) == L"Untouched host");
        REQUIRE(DestroyWindow(host));
        CHECK(second.IsCreated());
        REQUIRE(shell.Destroy() == LWS::Result::Success);
        CHECK_FALSE(second.IsCreated());
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}

TEST_CASE("External parenting validation leaves failed creation retryable", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    {
        LWS::Window child(platform), managed(platform), retry(platform);
        CHECK(LWS::Win32::CreateChild(child, nullptr) == LWS::Result::InvalidArgument);
        REQUIRE(managed.Create() == LWS::Result::Success);
        CHECK(LWS::Win32::CreateChild(child, *LWS::Win32::GetHwnd(managed)) == LWS::Result::InvalidArgument);
        std::promise<HWND> ready;
        auto handle = ready.get_future();
        std::latch release(1);
        std::jthread worker(
            [&]
            {
                auto window = CreateWindowExW(0, L"STATIC", L"Other thread", 0, 0, 0, 10, 10, nullptr, nullptr,
                                              GetModuleHandleW(nullptr), nullptr);
                ready.set_value(window);
                release.wait();
                DestroyWindow(window);
            });
        const auto foreign = handle.get();
        CHECK(foreign != nullptr);
        CHECK(LWS::Win32::CreateChild(child, foreign) == LWS::Result::InvalidArgument);
        release.count_down();
        worker.join();
        const HWND host = CreateWindowExW(0, L"STATIC", L"External parent", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480,
                                          nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        REQUIRE(host);
        CHECK(LWS::Win32::CreateChild(child, host, {.parent = &managed}) == LWS::Result::InvalidArgument);
        CHECK(LWS::Win32::CreateChild(child, host, {.clientSize = {0, 10}}) == LWS::Result::InvalidArgument);
        REQUIRE(LWS::Win32::CreateChild(child, host) == LWS::Result::Success);
        CHECK(LWS::Win32::CreateChild(child, host) == LWS::Result::InvalidState);
        CHECK(child.Create() == LWS::Result::InvalidState);
        REQUIRE(child.Destroy() == LWS::Result::Success);
        CHECK(LWS::Win32::CreateChild(child, host) == LWS::Result::InvalidState);
        CHECK(LWS::Win32::CreateChild(retry, host, {.clientSize = {0, 10}}) == LWS::Result::InvalidArgument);
        REQUIRE(retry.Create() == LWS::Result::Success);
        CHECK((GetWindowLongPtrW(*LWS::Win32::GetHwnd(retry), GWL_STYLE) & WS_CHILD) == 0);
        REQUIRE(DestroyWindow(host));
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}

TEST_CASE("A native host may release its owned plugin container during destruction", "[win32][hosting]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    {
        auto container = std::make_unique<LWS::Window>(platform);
        WNDCLASSW nativeClass{};
        nativeClass.hInstance = GetModuleHandleW(nullptr);
        nativeClass.lpszClassName = L"LWS.Test.ExternalParent.Release";
        nativeClass.lpfnWndProc = [](HWND window, UINT message, WPARAM wParam, LPARAM lParam) -> LRESULT
        {
            if (message == WM_NCCREATE)
                SetWindowLongPtrW(window, GWLP_USERDATA,
                                  reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
            else if (message == WM_DESTROY)
                reinterpret_cast<std::unique_ptr<LWS::Window>*>(GetWindowLongPtrW(window, GWLP_USERDATA))->reset();
            return DefWindowProcW(window, message, wParam, lParam);
        };
        REQUIRE(RegisterClassW(&nativeClass));
        const HWND host = CreateWindowExW(0, nativeClass.lpszClassName, L"Host owner", 0, 0, 0, 640, 480, nullptr,
                                          nullptr, nativeClass.hInstance, &container);
        REQUIRE(host);
        REQUIRE(LWS::Win32::CreateChild(*container, host) == LWS::Result::Success);
        unsigned beginning{}, ended{};
        auto listener = container->Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                    ++beginning;
                if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                    ++ended;
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(listener);
        REQUIRE(DestroyWindow(host));
        CHECK_FALSE(container);
        CHECK(beginning == 1);
        CHECK(ended == 1);
        REQUIRE(UnregisterClassW(nativeClass.lpszClassName, nativeClass.hInstance));
    }
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}
TEST_CASE("External child creation adopts host DPI without changing its caller", "[win32][hosting][dpi]")
{
    using GetContext = HANDLE(WINAPI*)();
    using SetContext = HANDLE(WINAPI*)(HANDLE);
    using WindowContext = HANDLE(WINAPI*)(HWND);
    using Equal = BOOL(WINAPI*)(HANDLE, HANDLE);
    const auto user = GetModuleHandleW(L"user32.dll");
    const auto get = reinterpret_cast<GetContext>(GetProcAddress(user, "GetThreadDpiAwarenessContext"));
    const auto set = reinterpret_cast<SetContext>(GetProcAddress(user, "SetThreadDpiAwarenessContext"));
    const auto windowContext = reinterpret_cast<WindowContext>(GetProcAddress(user, "GetWindowDpiAwarenessContext"));
    const auto equal = reinterpret_cast<Equal>(GetProcAddress(user, "AreDpiAwarenessContextsEqual"));
    if (!get || !set || !windowContext || !equal)
        SKIP("Mixed DPI contexts require Windows 10 DPI APIs");
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    const HANDLE original = get();
    const HANDLE previous = set(DPI_AWARENESS_CONTEXT_UNAWARE);
    const HWND host = LwsTest::CreateHost(L"Unaware host", 0);
    set(previous);
    REQUIRE(host);
    {
        const LWS::Win32::ScopedWindowDpiContext context(host);
        CHECK(equal(get(), windowContext(host)));
    }
    CHECK(equal(get(), original));
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    {
        LWS::Window child(platform);
        REQUIRE(LWS::Win32::CreateChild(child, host) == LWS::Result::Success);
        CHECK(equal(windowContext(*LWS::Win32::GetHwnd(child)), windowContext(host)));
        CHECK(equal(get(), original));
    }
    REQUIRE(DestroyWindow(host));
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}
TEST_CASE("External native parents retain child restrictions alongside popups and snapping",
          "[win32][hosting][popup][snapping]")
{
    LwsTest::Context initialized;
    auto& platform = initialized.value;
    const HWND host = LwsTest::CreateHost(L"External parent", 0);
    REQUIRE(host);
    {
        LWS::Window container(platform);
        CHECK(LWS::Win32::CreateChild(container, host, {.snappingMode = LWS::WindowSnappingMode::OtherWindows}) ==
              LWS::Result::NotSupported);
        CHECK(LWS::Win32::CreateChild(container, host, {.snappingRole = LWS::WindowSnappingRole::Leader}) ==
              LWS::Result::NotSupported);
        REQUIRE(LWS::Win32::CreateChild(container, host) == LWS::Result::Success);
        CHECK(container.SetSnappingMode(LWS::WindowSnappingMode::OtherWindows) == LWS::Result::NotSupported);
        CHECK(container.SetSnappingRole(LWS::WindowSnappingRole::Follower) == LWS::Result::NotSupported);
        LWS::Window popup(platform);
        REQUIRE(popup.Create({.parent = &container, .popupPlacement = LWS::PopupPlacement{.size = {50, 30}}}) ==
                LWS::Result::Success);
        CHECK(popup.IsPopup());
        CHECK(popup.Reparent(container) == LWS::Result::NotSupported);
        CHECK(popup.GetParent() == &container);
    }
    REQUIRE(DestroyWindow(host));
    REQUIRE(platform.Shutdown() == LWS::Result::Success);
}
#endif
