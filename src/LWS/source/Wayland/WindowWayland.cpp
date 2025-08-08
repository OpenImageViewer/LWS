// Wayland-specific WindowWayland extension methods.
#include <LWS/Wayland/WindowWayland.hpp>

#ifdef LWS_PLATFORM_WAYLAND

namespace LWS::Wayland
{
    void WindowWayland::SetAppId(const std::string& appId)
    {
        fAppId = appId;
        // TODO: cast impl_ to WindowBackendWayland, call xdg_toplevel_set_app_id
        //   if (auto* b = getBackendAs<WindowBackendWayland>())
        //       b->setAppId(appId);
    }

    std::string WindowWayland::GetAppId() const
    {
        return fAppId;
    }
}

#endif // LWS_PLATFORM_WAYLAND
