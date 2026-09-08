#pragma once

#include <LWS/Result.hpp>

#include <expected>
#include <string_view>

struct wl_display;
struct wl_surface;

namespace LWS
{
    class Window;
}

namespace LWS::Wayland
{
    [[nodiscard]] std::expected<wl_surface*, Result> GetSurface(Window& window);
    [[nodiscard]] std::expected<wl_surface*, Result> GetSurface(const Window& window);
    [[nodiscard]] std::expected<wl_display*, Result> GetDisplay(Window& window);
    [[nodiscard]] Result SetAppId(Window& window, std::string_view appId);
}  // namespace LWS::Wayland
