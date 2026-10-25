#pragma once
#include <Windows.h>

namespace LWS::internal
{
    [[nodiscard]] bool isOleInitializedForCurrentThread();
    void dispatchWindowTimer(HWND window, UINT_PTR id);
}  // namespace LWS::internal
