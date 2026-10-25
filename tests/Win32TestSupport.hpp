#pragma once
#ifdef LWS_HAS_WIN32_BACKEND
    #include <catch2/catch_test_macros.hpp>
    #include <LWS/Platform.hpp>
    #include <LWS/Win32/Platform.hpp>
    #include <Windows.h>
namespace LwsTest
{
    class Context final
    {
      public:

        explicit Context(LWS::DispatchMode mode = LWS::DispatchMode::OwnedLoop)
        {
            REQUIRE(LWS::Win32::BootstrapProcess() == LWS::Result::Success);
            REQUIRE(value.Init({.backend = LWS::BackendId::Win32, .dispatchMode = mode}) == LWS::Result::Success);
        }
        LWS::PlatformContext value;
    };
    inline HWND CreateHost(const wchar_t* title, DWORD style = 0)
    {
        const HWND host = CreateWindowExW(0, L"STATIC", title, style, 0, 0, 640, 480, nullptr, nullptr,
                                          GetModuleHandleW(nullptr), nullptr);
        REQUIRE(host != nullptr);
        return host;
    }
    inline void DispatchHostMessages()
    {
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
        {
            REQUIRE(message.message != WM_QUIT);
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
}  // namespace LwsTest
#endif
