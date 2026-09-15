#include <catch2/catch_test_macros.hpp>
#include <LWS/Window.hpp>
#include <LWS/Clipboard.hpp>
#include <LWS/Timer.hpp>
#include <LWS/source/internal/PlatformBackend.hpp>
#include <LWS/source/internal/WindowBackendAccess.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/source/Win32/internal/DragAndDropTarget.hpp>
#elif defined(LWS_HAS_WAYLAND_BACKEND)
    #include <LWS/Wayland/WindowExtensions.hpp>
#endif
#include <memory>
#include <thread>
#include <vector>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <string>
#include <chrono>
#ifndef NDEBUG
    #ifdef LWS_HAS_WIN32_BACKEND
        #include <crtdbg.h>
    #else
        #include <sys/wait.h>
        #include <sys/resource.h>
        #include <unistd.h>
        #include <signal.h>
    #endif
#endif
#include <LLUtils/Event.h>
#ifdef LWS_HAS_WAYLAND_BACKEND
    #include <wayland-client.h>
    #include <sys/socket.h>
#endif

#if defined(LWS_HAS_WIN32_BACKEND) || defined(LWS_HAS_WAYLAND_BACKEND)
namespace
{
    void Start(LWS::PlatformContext& context)
    {
    #ifdef LWS_HAS_WIN32_BACKEND
        REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
        REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    #else
        REQUIRE(context.Init({.backend = LWS::BackendId::Wayland}) == LWS::Result::Success);
    #endif
    }
    bool HasHandle(LWS::Window& window)
    {
    #ifdef LWS_HAS_WIN32_BACKEND
        return LWS::Win32::GetHwnd(window).has_value();
    #else
        return LWS::Wayland::GetSurface(window).has_value() && LWS::Wayland::GetDisplay(window).has_value();
    #endif
    }
}  // namespace

TEST_CASE("Cleanup precedes child teardown and cannot be consumed", "[lifecycle][window]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Window parent(context), child(context), grandchild(context), rejected(context);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent}) == LWS::Result::Success);
    REQUIRE(grandchild.Create({.parent = &child}) == LWS::Result::Success);
    std::vector<int> order;
    unsigned reported = 0;
    context.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++reported; });
    auto first = parent.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
            {
                order.push_back(1);
                REQUIRE(HasHandle(parent));
                REQUIRE(HasHandle(child));
                REQUIRE(parent.Destroy() == LWS::Result::InvalidState);
                REQUIRE(child.Destroy() == LWS::Result::InvalidState);
                REQUIRE(rejected.Create({.parent = &parent}) == LWS::Result::InvalidState);
                REQUIRE(rejected.GetParent() == nullptr);
                REQUIRE(parent.SetTitle(LWS::string_type{}) == LWS::Result::InvalidState);
                REQUIRE(parent.SetMouseCursorVisible(false) == LWS::Result::InvalidState);
                return LWS::EventResponse::Handled;
            }
            return LWS::EventResponse::Unhandled;
        });
    auto throwing = parent.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                throw 42;
            return LWS::EventResponse::Unhandled;
        });
    auto last = parent.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
            {
                order.push_back(2);
                REQUIRE(child.IsCreated());
            }
            if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
            {
                order.push_back(6);
                REQUIRE_FALSE(HasHandle(parent));
            }
            return LWS::EventResponse::Unhandled;
        });
    auto c = child.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                order.push_back(3);
            if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                order.push_back(5);
            return LWS::EventResponse::Unhandled;
        });
    auto g = grandchild.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                REQUIRE(HasHandle(grandchild));
            if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                order.push_back(4);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(first);
    REQUIRE(throwing);
    REQUIRE(last);
    REQUIRE(c);
    REQUIRE(g);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE(order == std::vector<int>{1, 2, 3, 4, 5, 6});
    REQUIRE(reported == 1);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE(order.size() == 6);
}

TEST_CASE("A child cleanup callback cannot destroy its live ancestor", "[lifecycle][window]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Window parent(context), child(context);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent}) == LWS::Result::Success);
    auto connection = child.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventWindowDestroying>(event))
            {
                REQUIRE(parent.Destroy() == LWS::Result::InvalidState);
                REQUIRE(HasHandle(parent));
            }
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection);
    REQUIRE(child.Destroy() == LWS::Result::Success);
    REQUIRE(parent.IsCreated());
    REQUIRE(parent.Destroy() == LWS::Result::Success);
}

TEST_CASE("Fatal failure invalidates handles and preserves diagnostic through teardown", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    {
        LWS::Window window(context);
        REQUIRE(window.Create() == LWS::Result::Success);
        bool cleaned = false;
        auto listener = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (const auto* destroying = std::get_if<LWS::EventWindowDestroying>(&event))
                {
                    cleaned = true;
                    REQUIRE_FALSE(destroying->nativeResourcesAvailable);
                    REQUIRE_FALSE(HasHandle(window));
                }
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(listener);
        bool later = false;
        REQUIRE(context.PostTask([&] { LWS::internal::PlatformContextAccess::Fail(context, 123, "injected pump"); }) ==
                LWS::Result::Success);
        REQUIRE(context.PostTask([&] { later = true; }) == LWS::Result::Success);
        REQUIRE(context.RunMessageLoop() == LWS::LoopResult::Failed);
        REQUIRE_FALSE(later);
        REQUIRE_FALSE(window.IsCreated());
        REQUIRE_FALSE(HasHandle(window));
        REQUIRE(window.SetVisible(true) == LWS::Result::InvalidState);
        REQUIRE_FALSE(window.Listen([](const auto&) { return LWS::EventResponse::Unhandled; }));
        REQUIRE_FALSE(context.Supports(LWS::PlatformFeature::Clipboard));
        REQUIRE(context.PostTask([] {}) == LWS::Result::InvalidState);
        context.RequestQuit();
        REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
        LWS::internal::PlatformContextAccess::Fail(context, 456, "secondary error");
        REQUIRE(context.GetFailure()->nativeError == 123);
        REQUIRE(window.Destroy() == LWS::Result::Success);
        REQUIRE(cleaned);
    }
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.GetFailure()->operation == "injected pump");
    REQUIRE(context.RunMessageLoop() == LWS::LoopResult::Failed);
    LWS::PlatformContext replacement;
    Start(replacement);
    REQUIRE(replacement.ProcessMessages() == LWS::LoopResult::Continue);
}

TEST_CASE("Nested failure retains task captures until active dispatch unwinds", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    auto owned = std::make_shared<LWS::Clipboard>(context);
    const std::weak_ptr<LWS::Clipboard> lifetime = owned;
    bool nestedReturned = false, later = false;
    REQUIRE(context.PostTask(
                [&]
                {
                    REQUIRE(
                        context.PostTask([&] { LWS::internal::PlatformContextAccess::Fail(context, 9, "nested"); }) ==
                        LWS::Result::Success);
                    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
                    REQUIRE_FALSE(lifetime.expired());
                    REQUIRE(context.Shutdown() == LWS::Result::InvalidState);
                    nestedReturned = true;
                }) == LWS::Result::Success);
    REQUIRE(context.PostTask([&, owned = std::move(owned)] { later = true; }) == LWS::Result::Success);
    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
    REQUIRE(nestedReturned);
    REQUIRE_FALSE(later);
    REQUIRE(lifetime.expired());
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Failed task retirement permits moved-from destructor reentry", "[lifecycle][failure][tasks]")
{
    unsigned cleanups = 0;
    LWS::PlatformContext context;
    Start(context);
    struct Cleanup
    {
        LWS::PlatformContext& context;
        unsigned& calls;
        Cleanup(LWS::PlatformContext& context, unsigned& calls) : context(context), calls(calls) {}
        Cleanup(Cleanup&&) noexcept = default;
        ~Cleanup()
        {
            if (context.GetFailure())
            {
                context.RequestQuit();
                ++calls;
            }
        }
        void operator()() const {}
    };
    REQUIRE(context.PostTask([&] { LWS::internal::PlatformContextAccess::Fail(context, 1, "retirement"); }) ==
            LWS::Result::Success);
    REQUIRE(context.PostTask(Cleanup(context, cleanups)) == LWS::Result::Success);
    SECTION("message pump")
    {
        REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
    }
    SECTION("shutdown")
    {
        REQUIRE(context.Shutdown() == LWS::Result::Success);
    }
    REQUIRE(cleanups > 0);
    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Failure inside portable dispatch suppresses remaining ordinary listeners", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    bool later = false;
    auto first = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event))
                LWS::internal::PlatformContextAccess::Fail(context, 9, "callback failure");
            return LWS::EventResponse::Unhandled;
        });
    auto second = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            if (std::holds_alternative<LWS::EventPaint>(event))
                later = true;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(first);
    REQUIRE(second);
    std::ignore = LWS::internal::WindowBackendAccess::Dispatch(window, LWS::EventPaint{});
    REQUIRE_FALSE(later);
    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
}

TEST_CASE("Normal quit is sticky and shutdown still drains accepted work", "[lifecycle][platform]")
{
    LWS::PlatformContext context;
    Start(context);
    bool drained = false;
    context.RequestQuit();
    REQUIRE(context.PostTask(
                [&]
                {
                    drained = true;
                    REQUIRE(context.PostTask([] {}) == LWS::Result::InvalidState);
                }) == LWS::Result::Success);
    REQUIRE(context.ProcessMessages() == LWS::LoopResult::Quit);
    REQUIRE(context.RunMessageLoop() == LWS::LoopResult::Quit);
    REQUIRE_FALSE(drained);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(drained);
    REQUIRE_FALSE(context.GetFailure());
}

TEST_CASE("Failure during shutdown discards the remaining accepted tasks", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    bool later = false;
    REQUIRE(context.PostTask([&] { LWS::internal::PlatformContextAccess::Fail(context, 1, "shutdown"); }) ==
            LWS::Result::Success);
    REQUIRE(context.PostTask([&] { later = true; }) == LWS::Result::Success);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE_FALSE(later);
    REQUIRE(context.PostTask([] {}) == LWS::Result::InvalidState);
    REQUIRE(context.GetFailure()->nativeError == 1);
}

TEST_CASE("Discarded captures may reenter failed shutdown", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Result nested = LWS::Result::Failure;
    struct Cleanup
    {
        LWS::PlatformContext& context;
        LWS::Result& result;
        ~Cleanup() { result = context.Shutdown(); }
    };
    auto cleanup = std::make_shared<Cleanup>(context, nested);
    REQUIRE(context.PostTask([cleanup = std::move(cleanup)] {}) == LWS::Result::Success);
    LWS::internal::PlatformContextAccess::Fail(context, 1, "before shutdown");
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(nested == LWS::Result::Success);
    REQUIRE(context.GetFailure()->nativeError == 1);
}

TEST_CASE("Conditional service propagation stops after failure and preserves ordinary Raise", "[lifecycle][failure]")
{
    LWS::PlatformContext context;
    Start(context);
    LLUtils::Event<void()> serviceEvent;
    unsigned calls = 0;
    serviceEvent.Add(
        [&]
        {
            ++calls;
            LWS::internal::PlatformContextAccess::Fail(context, 1, "service callback");
        });
    serviceEvent.Add([&] { ++calls; });
    serviceEvent.RaiseWhile([&] { return context.IsUsable(); });
    REQUIRE(calls == 1);
    serviceEvent.Raise();
    REQUIRE(calls == 3);
}

TEST_CASE("Destructor cleanup can disconnect its own listener", "[lifecycle][window]")
{
    LWS::PlatformContext context;
    Start(context);
    unsigned calls = 0;
    LWS::EventConnection connection;
    {
        LWS::Window window(context);
        REQUIRE(window.Create() == LWS::Result::Success);
        auto registered = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventWindowDestroying>(event))
                {
                    ++calls;
                    REQUIRE(HasHandle(window));
                    connection.Disconnect();
                }
                return LWS::EventResponse::Unhandled;
            });
        REQUIRE(registered);
        connection = std::move(*registered);
    }
    REQUIRE(calls == 1);
    REQUIRE_FALSE(connection.IsConnected());
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Failure remains local to one UI thread", "[lifecycle][failure][thread]")
{
    LWS::PlatformContext failed;
    Start(failed);
    std::atomic<bool> ready{}, proceed{}, healthy{};
    const auto backend = *failed.GetBackendId();
    std::thread other(
        [&]
        {
            LWS::PlatformContext context;
            const bool initialized = context.Init({.backend = backend}) == LWS::Result::Success;
            ready = true;
            while (!proceed.load())
                std::this_thread::yield();
            healthy = initialized && context.ProcessMessages() == LWS::LoopResult::Continue && context.IsUsable();
        });
    while (!ready.load())
        std::this_thread::yield();
    LWS::internal::PlatformContextAccess::Fail(failed, 1, "isolated");
    proceed = true;
    other.join();
    REQUIRE(healthy.load());
}

    #ifdef LWS_HAS_WAYLAND_BACKEND
TEST_CASE("Native Wayland disconnect becomes a fatal diagnostic", "[lifecycle][failure][wayland]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto display = LWS::Wayland::GetDisplay(window);
    REQUIRE(display);
    REQUIRE(::shutdown(wl_display_get_fd(*display), SHUT_RDWR) == 0);
    SECTION("pump")
    {
        REQUIRE(context.ProcessMessages() == LWS::LoopResult::Failed);
    }
    SECTION("monitor roundtrip")
    {
        REQUIRE(context.RefreshMonitors() == LWS::Result::Failure);
    }
    REQUIRE(context.GetFailure());
    REQUIRE_FALSE(HasHandle(window));
    REQUIRE(window.Destroy() == LWS::Result::Success);
}
    #endif

    #ifndef NDEBUG
TEST_CASE("Lifetime misuse is diagnosed before releasing owners", "[lifecycle][assert]")
{
    if (const char* probe = std::getenv("LWS_ASSERT_PROBE"))
    {
        #ifdef LWS_HAS_WIN32_BACKEND
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
        #else
        const rlimit limit{0, 0};
        setrlimit(RLIMIT_CORE, &limit);
        #endif
        auto context = std::make_unique<LWS::PlatformContext>();
        Start(*context);
        const int scenario = std::atoi(probe);
        if (scenario == 1)
        {
            auto window = std::make_unique<LWS::Window>(*context);
            REQUIRE(window->Create() == LWS::Result::Success);
            auto listener = window->Listen(
                [&](const LWS::AnyEvent&)
                {
                    window.reset();
                    return LWS::EventResponse::Unhandled;
                });
            REQUIRE(listener);
            std::ignore = LWS::internal::WindowBackendAccess::Dispatch(*window, LWS::EventPaint{});
        }
        else if (scenario == 2)
        {
            REQUIRE(context->PostTask([&] { context.reset(); }) == LWS::Result::Success);
            std::ignore = context->ProcessMessages();
        }
        else if (scenario == 3)
        {
            LWS::Window window(*context);
            std::thread wrongThread([&] { std::ignore = window.GetTitle(); });
            wrongThread.join();
        }
        else if (scenario == 4)
        {
            REQUIRE(context->PostTask([] { throw 42; }) == LWS::Result::Success);
            std::ignore = context->ProcessMessages();
        }
        else
        {
            LWS::Window window(*context);
            auto listener = window.Listen([](const auto&) { return LWS::EventResponse::Unhandled; });
            REQUIRE(listener);
            std::thread wrongThread([&] { listener->Disconnect(); });
            wrongThread.join();
        }
        std::exit(0);  // Reaching this point means the misuse was not diagnosed.
    }
    for (int scenario = 1; scenario <= 5; ++scenario)
    {
        INFO("assertion scenario " << scenario);
        #ifdef LWS_HAS_WIN32_BACKEND
        wchar_t executable[32768]{};
        REQUIRE(GetModuleFileNameW(nullptr, executable, 32768) != 0);
        const std::wstring value = std::to_wstring(scenario);
        REQUIRE(SetEnvironmentVariableW(L"LWS_ASSERT_PROBE", value.c_str()));
        std::wstring command = L"\"" + std::wstring(executable) + L"\" \"[assert]\" --reporter compact";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        const BOOL launched = CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                             nullptr, nullptr, &startup, &process);
        SetEnvironmentVariableW(L"LWS_ASSERT_PROBE", nullptr);
        REQUIRE(launched);
        const DWORD waited = WaitForSingleObject(process.hProcess, 10000);
        if (waited != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 99);
        DWORD result{};
        GetExitCodeProcess(process.hProcess, &result);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        REQUIRE(waited == WAIT_OBJECT_0);
        REQUIRE((result == 3 || result == 0x40000015UL || result == 0xC0000409UL));
        #else
        const auto child = fork();
        REQUIRE(child >= 0);
        if (child == 0)
        {
            std::ignore = std::freopen("/dev/null", "w", stdout);
            std::ignore = std::freopen("/dev/null", "w", stderr);
            setenv("LWS_ASSERT_PROBE", std::to_string(scenario).c_str(), 1);
            execl("/proc/self/exe", "LWSTests", "[assert]", "--reporter", "compact", nullptr);
            _exit(99);
        }
        int status{};
        pid_t waited = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (waited == 0 && std::chrono::steady_clock::now() < deadline)
        {
            waited = waitpid(child, &status, WNOHANG);
            if (waited == 0)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (waited == 0)
        {
            kill(child, SIGKILL);
            waitpid(child, &status, 0);
        }
        REQUIRE(waited == child);
        REQUIRE(WIFSIGNALED(status));
        REQUIRE(WTERMSIG(status) == SIGABRT);
        #endif
    }
}
    #endif

    #ifdef LWS_HAS_WIN32_BACKEND
TEST_CASE("A drop callback may revoke and release its native target", "[lifecycle][dragdrop][win32]")
{
    struct Files final : IDataObject
    {
        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void**) override { return E_NOINTERFACE; }
        ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
        ULONG STDMETHODCALLTYPE Release() override { return 1; }
        HRESULT STDMETHODCALLTYPE GetData(FORMATETC*, STGMEDIUM* result) override
        {
            static constexpr wchar_t names[] = L"C:\\first.png\0C:\\second.png\0";
            const auto memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DROPFILES) + sizeof(names));
            if (!memory)
                return E_OUTOFMEMORY;
            auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
            if (!drop)
            {
                GlobalFree(memory);
                return E_OUTOFMEMORY;
            }
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            std::memcpy(reinterpret_cast<std::byte*>(drop) + sizeof(DROPFILES), names, sizeof(names));
            GlobalUnlock(memory);
            result->tymed = TYMED_HGLOBAL;
            result->hGlobal = memory;
            result->pUnkForRelease = nullptr;
            return S_OK;
        }
        HRESULT STDMETHODCALLTYPE GetDataHere(FORMATETC*, STGMEDIUM*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE QueryGetData(FORMATETC*) override { return S_OK; }
        HRESULT STDMETHODCALLTYPE GetCanonicalFormatEtc(FORMATETC*, FORMATETC*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumFormatEtc(DWORD, IEnumFORMATETC**) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE DUnadvise(DWORD) override { return E_NOTIMPL; }
        HRESULT STDMETHODCALLTYPE EnumDAdvise(IEnumSTATDATA**) override { return E_NOTIMPL; }
    } files;
    LWS::PlatformContext context;
    Start(context);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    const auto hwnd = LWS::Win32::GetHwnd(window);
    REQUIRE(hwnd);
    auto capture = std::make_shared<int>(1);
    const std::weak_ptr<int> lifetime = capture;
    unsigned calls = 0;
    LWS::internal::DragAndDropTarget* target = nullptr;
    target = new LWS::internal::DragAndDropTarget(
        *hwnd,
        [&, capture = std::move(capture)](const auto&)
        {
            ++calls;
            target->detach();
            target->Release();  // release the native-window owner's reference while Drop is active
            REQUIRE_FALSE(lifetime.expired());
        });
    REQUIRE(SUCCEEDED(target->getAttachResult()));
    DWORD effect = DROPEFFECT_COPY;
    REQUIRE(target->Drop(&files, 0, POINTL{}, &effect) == S_OK);
    REQUIRE(calls == 1);
    REQUIRE(lifetime.expired());
}
    #endif

TEST_CASE("Invalid window requests preserve state", "[lifecycle][capability]")
{
    LWS::PlatformContext context;
    Start(context);
    LWS::Window window(context);
    #ifdef LWS_HAS_WAYLAND_BACKEND
    LWS::WindowConfig unsupported;
    unsupported.alwaysOnTop = true;
    REQUIRE(window.Create(unsupported) == LWS::Result::NotSupported);
    REQUIRE_FALSE(window.IsCreated());
    REQUIRE_FALSE(window.GetAlwaysOnTop());
    #endif
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetWindowMode(static_cast<LWS::WindowMode>(99)) == LWS::Result::InvalidArgument);
    REQUIRE(window.GetWindowMode() == LWS::WindowMode::Windowed);
    const auto state = window.GetShowState();
    REQUIRE(window.RequestShowState(static_cast<LWS::WindowShowState>(99)) == LWS::Result::InvalidArgument);
    REQUIRE(window.GetShowState() == state);
    #ifdef LWS_HAS_WAYLAND_BACKEND
    REQUIRE(window.SetAlwaysOnTop(true) == LWS::Result::NotSupported);
    REQUIRE_FALSE(window.GetAlwaysOnTop());
    #endif
}
#endif
