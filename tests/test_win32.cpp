#ifdef LWS_PLATFORM_WIN32

    #include <catch2/catch_test_macros.hpp>

    #include <LWS/Clipboard.hpp>
    #include <LWS/Cursor.hpp>
    #include <LWS/FileDialog.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Timer.hpp>
    #include <LWS/Win32/Platform.hpp>
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
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
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
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
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
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
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
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
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
    REQUIRE(window.GetClientSize() == LWS::LogicalSize{720, 520});
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

TEST_CASE("Failed creation preserves a window icon for retry", "[window][icon][win32]")
{
    bool iconAppliedAfterRetry{};
    std::jthread thread(
        [&]
        {
            const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            if (FAILED(comResult))
                return;

            {
                LWS::PlatformContext platform;
                if (LWS::Win32::BootstrapProcess() == LWS::Result::Success &&
                    platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success)
                {
                    LWS::Window window(platform);
                    const auto icon = MakeWindowIcon(std::byte{255});
                    if (icon.has_value() && window.SetWindowIcon(*icon) == LWS::Result::Success &&
                        window.Create({.dragAndDropEnabled = true}) == LWS::Result::NotSupported &&
                        window.Create() == LWS::Result::Success)
                    {
                        const HWND handle = *LWS::Win32::GetHwnd(window);
                        const HICON big = WindowIconHandle(handle, ICON_BIG);
                        iconAppliedAfterRetry = big != nullptr && WindowIconHandle(handle, ICON_SMALL) == big;
                    }
                }
            }
            CoUninitialize();
        });
    thread.join();

    REQUIRE(iconAppliedAfterRetry);
}

TEST_CASE("Window min and max client sizes round-trip", "[window][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create() == LWS::Result::Success);
    REQUIRE(window.SetMinMaxClientSize({100, 120}, {900, 700}) == LWS::Result::Success);
    REQUIRE(window.GetMinClientSize() == LWS::LogicalSize{100, 120});
    REQUIRE(window.GetMaxClientSize() == LWS::LogicalSize{900, 700});
}

TEST_CASE("Window metrics are coherent", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.clientSize = {400, 300}}) == LWS::Result::Success);
    const auto size = window.GetClientAreaSize();
    REQUIRE(size.has_value());
    REQUIRE(size->logical == window.GetClientSize());
    REQUIRE(size->pixels.x > 0);
    REQUIRE(size->Scale().x > 0.0);
}

TEST_CASE("Client area events suppress duplicate native sizes", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    unsigned changes{};
    auto connection = window.Listen(
        [&](const LWS::AnyEvent& event)
        {
            changes += std::holds_alternative<LWS::EventClientAreaSizeChanged>(event);
            return LWS::EventResponse::Unhandled;
        });
    REQUIRE(connection.has_value());
    REQUIRE(window.Create({.clientSize = {400, 300}}) == LWS::Result::Success);
    REQUIRE(window.RequestClientSize({400, 300}) == LWS::Result::Success);
    const unsigned confirmedChanges = changes;
    REQUIRE(window.RequestClientSize({400, 300}) == LWS::Result::Success);
    REQUIRE(changes == confirmedChanges);
    REQUIRE(window.RequestClientSize({500, 350}) == LWS::Result::Success);
    REQUIRE(changes == confirmedChanges + 1);
    REQUIRE(window.GetClientAreaSize()->logical == LWS::LogicalSize{500, 350});
}

TEST_CASE("Child placement preserves logical client geometry", "[window][geometry][parent][win32]")
{
    Context context;
    LWS::Window parent(context.value);
    LWS::Window child(context.value);
    REQUIRE(parent.Create({.clientSize = {800, 600}}) == LWS::Result::Success);
    REQUIRE(child.Create({.parent = &parent, .position = LWS::Point{100, 80}, .clientSize = {200, 300}}) ==
            LWS::Result::Success);
    REQUIRE(child.GetPosition() == LWS::Point{100, 80});

    const HWND handle = Hwnd(child);
    SetWindowLongPtrW(handle, GWL_STYLE, GetWindowLongPtrW(handle, GWL_STYLE) | WS_VSCROLL);
    SetWindowPos(handle, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    REQUIRE(child.SetPlacement({.position = LWS::Point{300, 120}, .clientSize = {200, 300}}) == LWS::Result::Success);
    REQUIRE(child.GetPosition() == LWS::Point{300, 120});
    REQUIRE(child.GetClientSize() == LWS::LogicalSize{200, 300});
    REQUIRE(child.GetClientAreaSize()->logical == child.GetClientSize());
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

TEST_CASE("Native resize limits measure the logical client area", "[window][metrics][win32]")
{
    Context context;
    LWS::Window window(context.value);
    REQUIRE(window.Create({.clientSize = {400, 300},
                           .styles = LWS::WindowStyle::Caption | LWS::WindowStyle::ResizableBorder}) ==
            LWS::Result::Success);
    const HWND handle = Hwnd(window);
    RECT outer{};
    RECT client{};
    REQUIRE(GetWindowRect(handle, &outer));
    REQUIRE(GetClientRect(handle, &client));
    const LONG borderWidth = outer.right - outer.left - client.right;
    const LONG borderHeight = outer.bottom - outer.top - client.bottom;
    const auto scale = window.GetClientAreaSize()->Scale();
    REQUIRE(window.SetMinMaxClientSize({100, 120}, {900, 700}) == LWS::Result::Success);
    MINMAXINFO limits{};
    SendMessageW(handle, WM_GETMINMAXINFO, 0, reinterpret_cast<LPARAM>(&limits));
    REQUIRE(limits.ptMinTrackSize.x == static_cast<LONG>(std::lround(100 * scale.x)) + borderWidth);
    REQUIRE(limits.ptMinTrackSize.y == static_cast<LONG>(std::lround(120 * scale.y)) + borderHeight);
    REQUIRE(limits.ptMaxTrackSize.x == static_cast<LONG>(std::lround(900 * scale.x)) + borderWidth);
    REQUIRE(limits.ptMaxTrackSize.y == static_cast<LONG>(std::lround(700 * scale.y)) + borderHeight);
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
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
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

TEST_CASE("Initial client dimensions are preserved on each monitor", "[window][metrics][monitor][win32]")
{
    Context context;
    std::vector<RECT> monitors;
    REQUIRE(EnumDisplayMonitors(nullptr, nullptr,
        [](HMONITOR, HDC, LPRECT rectangle, LPARAM data) -> BOOL
        {
            reinterpret_cast<std::vector<RECT>*>(data)->push_back(*rectangle);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors)));
    const auto primary = context.value.GetPrimaryMonitor();
    REQUIRE(primary.has_value());
    const double systemScale = primary->contentScale.x;
    for (const RECT& monitor : monitors)
    {
        LWS::Window window(context.value);
        REQUIRE(window.Create({.position = LWS::Point{
                                  static_cast<int32_t>(std::lround((monitor.left + 50) / systemScale)),
                                  static_cast<int32_t>(std::lround((monitor.top + 50) / systemScale))},
                               .clientSize = {400, 300}}) == LWS::Result::Success);
        const auto size = window.GetClientSize();
        INFO("monitor origin " << monitor.left << ", " << monitor.top << "; system scale " << systemScale
             << "; actual logical client " << size.x << " x " << size.y
             << "; pixels " << window.GetClientAreaSize()->pixels.x << " x " << window.GetClientAreaSize()->pixels.y);
        REQUIRE(size == LWS::LogicalSize{400, 300});
    }
}

// These process-DPI scenarios must run in separate test processes before any normal context bootstrap.
TEST_CASE("Process bootstrap rejects a thread-only DPI override", "[.][bootstrap][win32]")
{
    REQUIRE(SetProcessDPIAware());
    const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    REQUIRE(previous != nullptr);
    const LWS::Result result = LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2});
    SetThreadDpiAwarenessContext(previous);
    REQUIRE(result == LWS::Result::Failure);
}

TEST_CASE("Process bootstrap recognizes process awareness behind a thread override", "[.][bootstrap][win32]")
{
    REQUIRE(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    const DPI_AWARENESS_CONTEXT previous = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    REQUIRE(previous != nullptr);
    const LWS::Result result = LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2});
    SetThreadDpiAwarenessContext(previous);
    REQUIRE(result == LWS::Result::Success);
}

#endif
