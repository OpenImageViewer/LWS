#ifdef LWS_PLATFORM_WIN32

    #include <catch2/catch_test_macros.hpp>

    #include <LWS/Clipboard.hpp>
    #include <LWS/Cursor.hpp>
    #include <LWS/FileDialog.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <LWS/Window.hpp>

    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>
    #include <Ole2.h>

    #include <array>
    #include <atomic>
    #include <chrono>
    #include <cstddef>
    #include <cmath>
    #include <thread>
    #include <type_traits>

namespace
{
    class Context final
    {
      public:

        Context()
        {
            REQUIRE(value.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
        }

        LWS::PlatformContext value;
    };

    HWND Hwnd(LWS::Window& window)
    {
        const auto handle = LWS::Win32::GetHwnd(window);
        REQUIRE(handle.has_value());
        return *handle;
    }

    std::expected<LWS::WindowIcon, LWS::Result> MakeWindowIcon(std::byte blue)
    {
        const std::array pixels{blue, std::byte{0}, std::byte{0}, std::byte{255}};
        return LWS::WindowIcon::FromBitmap({
            .pixels = pixels,
            .width = 1,
            .height = 1,
            .rowPitch = 4,
        });
    }

    HICON WindowIconHandle(HWND window, WPARAM size)
    {
        return reinterpret_cast<HICON>(SendMessageW(window, WM_GETICON, size, 0));
    }

    void Pump(LWS::PlatformContext& context)
    {
        std::ignore = context.ProcessMessages();
    }
}  // namespace

TEST_CASE("Platform context is one-shot", "[platform][win32]")
{
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    REQUIRE(context.IsUsable());
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::InvalidState);
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE_FALSE(context.IsUsable());
    REQUIRE(context.Shutdown() == LWS::Result::Success);
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::InvalidState);
}

TEST_CASE("Platform rejects invalid and unavailable backends", "[platform][win32]")
{
    LWS::PlatformContext context;
    REQUIRE(context.Init({}) == LWS::Result::InvalidArgument);
    REQUIRE(context.Init({.backend = LWS::BackendId::X11}) == LWS::Result::NotSupported);
}

TEST_CASE("Independent contexts run on independent UI threads", "[platform][thread][win32]")
{
    std::atomic_uint successes{};
    auto run = [&]
    {
        LWS::PlatformContext context;
        if (context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success)
        {
            ++successes;
            std::ignore = context.Shutdown();
        }
    };
    std::jthread first(run);
    std::jthread second(run);
    first.join();
    second.join();
    REQUIRE(successes == 2);
}

TEST_CASE("MTA context keeps ordinary windowing available", "[platform][com][win32]")
{
    std::jthread thread(
        []
        {
            REQUIRE(SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED)));
            {
                Context context;
                LWS::Window window(context.value);
                REQUIRE(window.Create() == LWS::Result::Success);
                REQUIRE(window.EnableDragAndDrop(true) != LWS::Result::Success);
            }
            CoUninitialize();
        });
}

TEST_CASE("Window is stable, final, and one-shot", "[window][win32]")
{
    STATIC_REQUIRE(std::is_final_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_default_constructible_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<LWS::Window>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<LWS::Window>);

    Context context;
    LWS::Window window(context.value);
    REQUIRE_FALSE(window.IsCreated());
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::InvalidState);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::InvalidState);
}

TEST_CASE("Context shutdown rejects a bound C++ window", "[platform][window][win32]")
{
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    {
        LWS::Window window(context);
        REQUIRE(context.Shutdown() == LWS::Result::InvalidState);
    }
    REQUIRE(context.Shutdown() == LWS::Result::Success);
}

TEST_CASE("Window basic properties round-trip", "[window][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.title = L"initial", .position = LWS::Point{80, 90}, .clientSize = {640, 480}}) ==
            LWS::Result::Success);

    REQUIRE(window.SetTitle(L"updated") == LWS::Result::Success);
    REQUIRE(window.GetTitle() == L"updated");
    REQUIRE(window.SetVisible(true) == LWS::Result::Success);
    REQUIRE(window.GetVisible());
    REQUIRE(window.SetPosition({120, 140}) == LWS::Result::Success);
    REQUIRE(window.GetPosition().has_value());
    REQUIRE(window.RequestClientSize({720, 520}) == LWS::Result::Success);
    REQUIRE(window.GetClientSize() == LWS::Size{720, 520});
    REQUIRE(window.SetAlwaysOnTop(true) == LWS::Result::Success);
    REQUIRE(window.GetAlwaysOnTop());
    REQUIRE(window.SetTransparent(true) == LWS::Result::Success);
    REQUIRE(window.GetTransparent());
}

TEST_CASE("Window icons apply before creation, replace, and reset", "[window][icon][win32]")
{
    Context context;
    LWS::Window window(context.value);
    const auto firstIcon = MakeWindowIcon(std::byte{255});
    REQUIRE(firstIcon.has_value());
    REQUIRE(window.SetWindowIcon(*firstIcon) == LWS::Result::Success);
    REQUIRE(window.Create({.visible = true}) == LWS::Result::Success);

    const HWND handle = Hwnd(window);
    const HICON initialBig = WindowIconHandle(handle, ICON_BIG);
    const HICON initialSmall = WindowIconHandle(handle, ICON_SMALL);
    REQUIRE(initialBig != nullptr);
    REQUIRE(initialSmall == initialBig);

    const auto replacement = MakeWindowIcon(std::byte{127});
    REQUIRE(replacement.has_value());
    REQUIRE(window.SetWindowIcon(*replacement) == LWS::Result::Success);
    const HICON replacementBig = WindowIconHandle(handle, ICON_BIG);
    REQUIRE(replacementBig != nullptr);
    REQUIRE(replacementBig != initialBig);
    REQUIRE(WindowIconHandle(handle, ICON_SMALL) == replacementBig);

    REQUIRE(window.ResetWindowIcon() == LWS::Result::Success);
    REQUIRE(WindowIconHandle(handle, ICON_BIG) == nullptr);
    REQUIRE(WindowIconHandle(handle, ICON_SMALL) == nullptr);
}

TEST_CASE("Window min and max client sizes round-trip", "[window][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMinMaxClientSize({100, 120}, {900, 700}) == LWS::Result::Success);
    REQUIRE(window.GetMinClientSize() == LWS::Size{100, 120});
    REQUIRE(window.GetMaxClientSize() == LWS::Size{900, 700});
}

TEST_CASE("Parent configuration creates a child relationship", "[window][parent][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    LWS::Window child(context.value);
    REQUIRE(parent.Create() == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .transparent = true}) == LWS::Result::Success);
    REQUIRE(child.GetParent() == &parent);
    REQUIRE(parent.Destroy() == LWS::Result::Success);
    REQUIRE_FALSE(child.IsCreated());
}

TEST_CASE("Portable event connections own listener registration", "[window][event][win32]")
{
    Context context;
    LWS::Window window(context.value);
    unsigned paints{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            paints += std::holds_alternative<LWS::EventPaint>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(connection->IsConnected());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(paints > 0);
    connection->Disconnect();
    REQUIRE_FALSE(connection->IsConnected());
}

TEST_CASE("Typed Win32 event connections are runtime validated", "[window][event][win32]")
{
    Context context;
    LWS::Window window(context.value);
    unsigned paints{};
    auto connection = LWS::Win32::Listen(window,
                                         [&](const LWS::Win32::PlatformEvent& event) -> std::optional<LRESULT>
                                         {
                                             paints += std::holds_alternative<LWS::Win32::PaintEvent>(event);
                                             return std::nullopt;
                                         });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(paints > 0);
}

TEST_CASE("Listener exceptions stop propagation and report through the context", "[window][event][exception][win32]")
{
    Context context;
    unsigned exceptions{};
    unsigned laterCalls{};
    context.value.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    LWS::Window window(context.value);
    auto throwing = window.Listen([](const LWS::AnyEvent&) -> LWS::EventResponse { throw 7; });
    auto later = window.Listen(
        [&](const LWS::AnyEvent&)
        {
            ++laterCalls;
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(throwing.has_value());
    REQUIRE(later.has_value());
    REQUIRE(window.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_PAINT, 0, 0);
    REQUIRE(exceptions == 1);
    REQUIRE(laterCalls == 0);
}

TEST_CASE("Unhandled close destroys and handled close retains", "[window][event][close][win32]")
{
    Context context;
    LWS::Window retained(context.value);
    auto connection = retained.Listen(
        [](const LWS::AnyEvent& event)
        {
            return std::holds_alternative<LWS::EventCloseRequested>(event) ? LWS::EventResponse::Handled
                                                                           : LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(retained.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(retained), WM_CLOSE, 0, 0);
    REQUIRE(retained.IsCreated());

    LWS::Window destroyed(context.value);
    REQUIRE(destroyed.Create() == LWS::Result::Success);
    SendMessageW(Hwnd(destroyed), WM_CLOSE, 0, 0);
    REQUIRE_FALSE(destroyed.IsCreated());
}

TEST_CASE("Posted tasks execute FIFO on the context thread", "[platform][task][win32]")
{
    Context context;
    std::vector<int> order;
    REQUIRE(context.value.PostTask([&] { order.push_back(1); }) == LWS::Result::Success);
    REQUIRE(context.value.PostTask([&] { order.push_back(2); }) == LWS::Result::Success);
    Pump(context.value);
    REQUIRE(order == std::vector{1, 2});
}

TEST_CASE("Task exceptions report and later work continues", "[platform][exception][win32]")
{
    Context context;
    unsigned exceptions{};
    bool laterRan{};
    context.value.SetUnhandledExceptionHandler([&](std::exception_ptr) noexcept { ++exceptions; });
    REQUIRE(context.value.PostTask([] { throw 7; }) == LWS::Result::Success);
    REQUIRE(context.value.PostTask([&] { laterRan = true; }) == LWS::Result::Success);
    Pump(context.value);
    REQUIRE(exceptions == 1);
    REQUIRE(laterRan);
}

TEST_CASE("Custom cursors validate and apply copied pixels", "[cursor][bitmap][win32]")
{
    const std::array pixels{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    auto cursor = LWS::Cursor::FromBitmap({.pixels = pixels, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(cursor.has_value());

    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMouseCursorVisible(false) == LWS::Result::Success);
    REQUIRE(window.ResetMouseCursor() == LWS::Result::Success);
}

TEST_CASE("Timer can target windows in its context", "[timer][win32]")
{
    Context context;
    LWS::Window first(context.value);
    LWS::Window second(context.value);
    LWS::Window uncreated(context.value);
    REQUIRE(first.Create() == LWS::Result::Success);
    REQUIRE(second.Create() == LWS::Result::Success);
    LWS::Timer timer(context.value);
    REQUIRE(timer.SetTargetWindow(&uncreated) == LWS::Result::InvalidState);
    REQUIRE(timer.SetTargetWindow(&first) == LWS::Result::Success);
    timer.SetInterval(25);
    REQUIRE(timer.SetTargetWindow(nullptr) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 25);
    REQUIRE(timer.SetTargetWindow(&second) == LWS::Result::Success);
    REQUIRE(timer.GetInterval() == 25);
}

TEST_CASE("Clipboard operations require an owned window", "[clipboard][win32]")
{
    Context context;
    LWS::Window window(context.value);
    LWS::Window uncreated(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Clipboard clipboard(context.value);
    const std::array<std::byte, 1> data{};
    REQUIRE(clipboard.SetClipboardData(uncreated, 1, data.data(), data.size()) != LWS::ClipboardResult::Success);
    REQUIRE(clipboard.SetClipboardData(window, 0, data.data(), data.size()) != LWS::ClipboardResult::Success);

    LWS::ListFileDialogFileNames files;
    REQUIRE(LWS::FileDialog::Show(LWS::FileDialogType::OpenFile, {}, {}, uncreated, {}, 1, {}, files) ==
            LWS::FileDialogResult::UnknownError);
}

TEST_CASE("Clipboard service ownership cannot be copied", "[clipboard][win32]")
{
    STATIC_REQUIRE_FALSE(std::is_copy_constructible_v<LWS::Clipboard>);
    STATIC_REQUIRE_FALSE(std::is_move_constructible_v<LWS::Clipboard>);
}

TEST_CASE("A shape replaces a custom cursor", "[cursor][win32]")
{
    const std::array pixels{std::byte{0}, std::byte{0}, std::byte{0}, std::byte{255}};
    const auto custom = LWS::Cursor::FromBitmap({.pixels = pixels, .width = 1, .height = 1, .rowPitch = 4}, {0, 0});
    REQUIRE(custom.has_value());
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMouseCursor(*custom) == LWS::Result::Success);
    REQUIRE(window.SetMouseCursor(LWS::Cursor::FromShape(LWS::CursorShape::Hand)) == LWS::Result::Success);
    SendMessageW(Hwnd(window), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(window)), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_HAND));
}

TEST_CASE("Retrieved timer messages cannot invoke a replacement timer", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto timer = std::make_unique<LWS::Timer>(context.value);
    REQUIRE(timer->SetTargetWindow(&window) == LWS::Result::Success);
    timer->SetInterval(1);
    MSG tick{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PeekMessageW(&tick, Hwnd(window), WM_TIMER, WM_TIMER, PM_REMOVE) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(tick.message == WM_TIMER);
    timer.reset();
    unsigned calls{};
    LWS::Timer replacement(context.value);
    REQUIRE(replacement.SetTargetWindow(&window) == LWS::Result::Success);
    replacement.SetCallback([&] { ++calls; });
    replacement.SetInterval(1);
    DispatchMessageW(&tick);
    REQUIRE(calls == 0);
}

TEST_CASE("Timer callbacks can destroy their timer", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    auto timer = std::make_unique<LWS::Timer>(context.value);
    REQUIRE(timer->SetTargetWindow(&window) == LWS::Result::Success);
    timer->SetCallback([&] { timer.reset(); });
    timer->SetInterval(1);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (timer && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(timer);
}

TEST_CASE("High precision callbacks can destroy their timer", "[timer][precision][lifetime][win32]")
{
    Context context;
    std::unique_ptr<LWS::HighPrecisionTimer> timer;
    timer = std::make_unique<LWS::HighPrecisionTimer>(context.value, [&] { timer.reset(); });
    timer->SetDueTime(0);
    timer->SetRepeatInterval(1);
    timer->Enable(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (timer && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE_FALSE(timer);
}

TEST_CASE("Disabling a high precision timer discards pending ticks", "[timer][precision][win32]")
{
    Context context;
    unsigned calls{};
    LWS::HighPrecisionTimer timer(context.value, [&] { ++calls; });
    timer.SetDueTime(0);
    timer.SetRepeatInterval(1);
    timer.Enable(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    timer.Enable(false);
    Pump(context.value);
    REQUIRE(calls == 0);
    timer.SetRepeatInterval(0);
    timer.Enable(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!calls && std::chrono::steady_clock::now() < deadline)
    {
        Pump(context.value);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(calls == 1);
    REQUIRE_FALSE(timer.GetEnabled());
}

TEST_CASE("Independent UI threads own independent timer registrations", "[timer][thread][win32]")
{
    std::atomic_uint successes{};
    auto run = [&]
    {
        LWS::PlatformContext context;
        if (context.Init({.backend = LWS::BackendId::Win32}) != LWS::Result::Success)
            return;
        LWS::Window window(context);
        if (window.Create() != LWS::Result::Success)
            return;
        unsigned calls{};
        for (unsigned iteration = 0; iteration < 100; ++iteration)
        {
            LWS::Timer timer(context);
            if (timer.SetTargetWindow(&window) != LWS::Result::Success)
                return;
            timer.SetCallback([&] { ++calls; });
            timer.SetInterval(1);
            if (iteration == 99)
            {
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (!calls && std::chrono::steady_clock::now() < deadline)
                {
                    std::ignore = context.ProcessMessages();
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
        if (calls > 0)
            ++successes;
    };
    std::jthread first(run);
    std::jthread second(run);
    first.join();
    second.join();
    REQUIRE(successes == 2);
}

TEST_CASE("Cursor visibility stays local to the window client area", "[cursor][visibility][win32]")
{
    Context context;
    LWS::Window hidden(context.value);
    LWS::Window visible(context.value);
    REQUIRE(hidden.Create() == LWS::Result::Success);
    REQUIRE(visible.Create() == LWS::Result::Success);
    const int initialVisibilityCount = ShowCursor(TRUE);
    ShowCursor(FALSE);
    REQUIRE(hidden.SetMouseCursorVisible(false) == LWS::Result::Success);
    SendMessageW(Hwnd(hidden), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(hidden)), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == nullptr);
    SendMessageW(Hwnd(visible), WM_SETCURSOR, reinterpret_cast<WPARAM>(Hwnd(visible)), MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    REQUIRE(GetCursor() == LoadCursorW(nullptr, IDC_ARROW));
    const int finalVisibilityCount = ShowCursor(TRUE);
    ShowCursor(FALSE);
    REQUIRE(finalVisibilityCount == initialVisibilityCount);
    REQUIRE(hidden.SetMouseCursorVisible(true) == LWS::Result::Success);
}

TEST_CASE("Posted work cannot starve native window input", "[platform][task][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    bool keyReceived{};
    auto connection = window.Listen([&](const LWS::AnyEvent& event)
    {
        if (std::holds_alternative<LWS::EventKeyDown>(event))
        {
            keyReceived = true;
            context.value.RequestQuit();
        }
        return LWS::EventResponse::Unhandled;
    });
    REQUIRE(connection.has_value());
    unsigned batches{};
    std::function<void()> postAgain;
    postAgain = [&]
    {
        if (++batches < 1000)
            std::ignore = context.value.PostTask(postAgain);
        else
            context.value.RequestQuit();
    };
    REQUIRE(context.value.PostTask(postAgain) == LWS::Result::Success);
    REQUIRE(PostMessageW(Hwnd(window), WM_KEYDOWN, 'A', 0));
    context.value.RunMessageLoop();
    REQUIRE(keyReceived);
    REQUIRE(batches < 1000);
}

TEST_CASE("Destroying a timer target detaches it and invalidates retrieved ticks", "[timer][lifetime][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    LWS::Timer timer(context.value);
    REQUIRE(timer.SetTargetWindow(&window) == LWS::Result::Success);
    unsigned calls{};
    timer.SetCallback([&] { ++calls; });
    timer.SetInterval(1);
    MSG tick{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!PeekMessageW(&tick, Hwnd(window), WM_TIMER, WM_TIMER, PM_REMOVE) &&
           std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    REQUIRE(tick.message == WM_TIMER);
    REQUIRE(window.Destroy() == LWS::Result::Success);
    DispatchMessageW(&tick);
    REQUIRE(calls == 0);
    REQUIRE_NOTHROW(timer.SetInterval(2));
    REQUIRE(timer.GetInterval() == 2);
}

// These process-DPI scenarios must run in separate test processes before any normal context bootstrap.

#endif
