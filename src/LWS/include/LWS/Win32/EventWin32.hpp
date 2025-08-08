#pragma once
#ifdef LWS_PLATFORM_WIN32
#include <cstdint>

namespace LWS::Win32
{
    /// Win32-only raw message — payload of EventRawPlatform when platformType == BackendId::Win32.
    struct WinMessage
    {
        uintptr_t hWnd;
        uint32_t message;
        uintptr_t wParam;
        uintptr_t lParam;
    };
}
#endif
