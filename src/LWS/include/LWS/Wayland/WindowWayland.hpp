#pragma once
#include <string>

#include <LWS/Window.hpp>

namespace LWS::Wayland
{
    /// Wayland-specific Window subclass.
    /// Exposes Wayland-only APIs that have no equivalent on other platforms.
    /// Include this header only when LWS_PLATFORM_WAYLAND is defined.
    ///
    /// Example usage:
    ///   #ifdef LWS_PLATFORM_WAYLAND
    ///   #include <LWS/Wayland/WindowWayland.hpp>
    ///   LWS::Wayland::WindowWayland win;
    ///   win.Create(config);
    ///   win.SetAppId("com.example.MyApp");  // xdg_toplevel_set_app_id
    ///   #endif
    class WindowWayland : public LWS::Window
    {
    public:
        /// Sets the application ID sent to the compositor via xdg_toplevel_set_app_id.
        /// The compositor may use this for desktop integration (taskbar grouping, .desktop file lookup).
        void SetAppId(const std::string& appId);
        std::string GetAppId() const;

    private:
        std::string fAppId;
    };
}
