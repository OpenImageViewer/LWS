#pragma once

struct wl_display;
struct wl_compositor;
struct wl_shm;
struct xdg_wm_base;
struct zxdg_decoration_manager_v1;

namespace LWS::internal
{
    [[nodiscard]] wl_display* getWlDisplay();
    [[nodiscard]] wl_compositor* getWlCompositor();
    [[nodiscard]] wl_shm* getWlShm();
    [[nodiscard]] xdg_wm_base* getXdgWmBase();
    [[nodiscard]] zxdg_decoration_manager_v1* getDecoManager();

    void incrementWindowCount();
    void decrementWindowCount();

    [[nodiscard]] bool isDisplayConnected();
    void invalidateDisplay();
}
