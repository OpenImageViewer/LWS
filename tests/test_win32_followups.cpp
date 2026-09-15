#ifdef LWS_HAS_WIN32_BACKEND
    #include <catch2/catch_test_macros.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Window.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>
    #include <array>
    #include <string>
    #include <thread>
    #include <future>
    #include <commctrl.h>

TEST_CASE("Unchanged immutable resources retain native objects across window transitions",
          "[cursor][icon][reuse][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext platform;
    REQUIRE(platform.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window first(platform), second(platform);
    std::array<std::byte, 16 * 16 * 4> pixels{};
    const LWS::BitmapBuffer bitmap{.pixels = pixels, .width = 16, .height = 16, .rowPitch = 64};
    auto cursor = LWS::Cursor::FromBitmap(bitmap, {2, 3});
    auto icon = LWS::WindowIcon::FromBitmap(bitmap);
    REQUIRE(cursor);
    REQUIRE(icon);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(first.Create() == LWS::Result::Success);
    REQUIRE(second.Create() == LWS::Result::Success);
    const HWND hwnd = *LWS::Win32::GetHwnd(first);
    const auto bigIcon = SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0);
    REQUIRE(bigIcon != 0);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    const HCURSOR firstCursor = GetCursor();
    REQUIRE(firstCursor != nullptr);
    REQUIRE(second.SetMouseCursor(*cursor) == LWS::Result::Success);
    const HCURSOR secondCursor = GetCursor();
    REQUIRE(secondCursor != nullptr);
    REQUIRE(secondCursor != firstCursor);
    auto workerCursor = std::async(std::launch::async, [value = *cursor] { return value; }).get();
    auto workerIcon = std::async(std::launch::async, [value = *icon] { return value; }).get();
    REQUIRE(first.SetMouseCursor(workerCursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetWindowIcon(workerIcon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    for (int i = 0; i < 20; ++i)
    {
        REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
        REQUIRE(GetCursor() == firstCursor);
        REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
        REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    }
    REQUIRE(first.SetMouseCursorVisible(false) == LWS::Result::Success);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == nullptr);
    REQUIRE(second.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == secondCursor);
    REQUIRE(first.SetMouseCursorVisible(true) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetVisible(true) == LWS::Result::Success);
    REQUIRE(first.SetVisible(false) == LWS::Result::Success);
    RECT rectangle{};
    REQUIRE(GetWindowRect(hwnd, &rectangle));
    SendMessageW(hwnd, WM_DPICHANGED, MAKELONG(144, 144), reinterpret_cast<LPARAM>(&rectangle));
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::Success);
    REQUIRE(GetCursor() == firstCursor);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    REQUIRE(LWS::Win32::GetHwnd(first) == hwnd);
    // Reapplying still reinstalls native properties, even though it reuses their allocation.
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, 0);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == bigIcon);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_SMALL, 0) == bigIcon);
    auto replacementCursor = LWS::Cursor::FromBitmap(bitmap, {1, 1});
    auto replacementIcon = LWS::WindowIcon::FromBitmap(bitmap);
    REQUIRE(replacementCursor);
    REQUIRE(replacementIcon);
    REQUIRE(first.SetMouseCursor(*replacementCursor) == LWS::Result::Success);
    REQUIRE(GetCursor() != firstCursor);
    REQUIRE(first.SetWindowIcon(*replacementIcon) == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) != bigIcon);
    REQUIRE(first.ResetMouseCursor() == LWS::Result::Success);
    REQUIRE(first.ResetWindowIcon() == LWS::Result::Success);
    REQUIRE(SendMessageW(hwnd, WM_GETICON, ICON_BIG, 0) == 0);
    REQUIRE(first.Destroy() == LWS::Result::Success);
    REQUIRE(first.SetMouseCursor(*cursor) == LWS::Result::InvalidState);
    REQUIRE(first.SetWindowIcon(*icon) == LWS::Result::InvalidState);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
}

TEST_CASE("Process bootstrap policy is idempotent and immutable", "[.][bootstrap][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) ==
            LWS::Result::InvalidState);
}

TEST_CASE("Failed DPI adoption can be retried after process configuration", "[.][bootstrap][win32]")
{
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) == LWS::Result::Failure);
    REQUIRE(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));
    REQUIRE(LWS::Win32::BootstrapProcess({LWS::Win32::DpiPolicy::AdoptExistingPerMonitorV2}) == LWS::Result::Success);
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::InvalidState);
}

TEST_CASE("Process DPI contracts run in isolated processes", "[bootstrap-suite][win32]")
{
    wchar_t executable[32768]{};
    REQUIRE(GetModuleFileNameW(nullptr, executable, 32768) != 0);
    for (const wchar_t* test : {L"Process bootstrap rejects a thread-only DPI override",
                                L"Process bootstrap recognizes process awareness behind a thread override",
                                L"Process bootstrap policy is idempotent and immutable",
                                L"Failed DPI adoption can be retried after process configuration"})
    {
        std::wstring command = L"\"" + std::wstring(executable) + L"\" \"" + test + L"\" --reporter compact";
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION process{};
        REQUIRE(CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                               &startup, &process));
        const DWORD waited = WaitForSingleObject(process.hProcess, 15000);
        if (waited != WAIT_OBJECT_0)
            TerminateProcess(process.hProcess, 99);
        DWORD result{};
        GetExitCodeProcess(process.hProcess, &result);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        INFO("DPI child exit code " << result);
        REQUIRE(waited == WAIT_OBJECT_0);
        REQUIRE(result == 0);
    }
}
TEST_CASE("Window style replacement updates the native frame once", "[win32][styles]")
{
    REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
    LWS::PlatformContext context;
    REQUIRE(context.Init({.backend = LWS::BackendId::Win32}) == LWS::Result::Success);
    LWS::Window window(context);
    REQUIRE(window.Create() == LWS::Result::Success);
    const auto hwnd = *LWS::Win32::GetHwnd(window);
    unsigned updates{};
    const SUBCLASSPROC observe = [](HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam, UINT_PTR,
                                    DWORD_PTR data) -> LRESULT
    {
        if (message == WM_STYLECHANGED && static_cast<int>(wparam) == GWL_STYLE)
            ++*reinterpret_cast<unsigned*>(data);
        return DefSubclassProc(hwnd, message, wparam, lparam);
    };
    REQUIRE(SetWindowSubclass(hwnd, observe, 1, reinterpret_cast<DWORD_PTR>(&updates)));
    const LWS::WindowStyleFlags styles(LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton);
    REQUIRE(window.SetWindowStyles(styles) == LWS::Result::Success);
    REQUIRE(window.GetWindowStyles() == styles);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
    REQUIRE(updates == 1);
    REQUIRE(window.SetWindowStyles(styles) == LWS::Result::Success);
    REQUIRE(updates == 1);
    REQUIRE(window.SetWindowStyles(LWS::WindowStyleFlags(LWS::WindowStyle::NoStyle)) == LWS::Result::Success);
    REQUIRE((GetWindowLongW(hwnd, GWL_STYLE) & WS_CAPTION) == 0);
    REQUIRE(updates == 2);
    REQUIRE(RemoveWindowSubclass(hwnd, observe, 1));
}
#endif
