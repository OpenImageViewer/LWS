#pragma once
#ifdef LWS_PLATFORM_X11
#include <LWS/Window.hpp>

namespace LWS
{
    class WindowX11 : public Window
    {
    public:
        WindowX11() = default;
    };
}
#endif