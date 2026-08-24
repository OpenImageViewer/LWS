#ifdef LWS_PLATFORM_WAYLAND
#include <LWS/Platform.hpp>
#include "internal/WaylandPlatformState.hpp"

#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>

#include <poll.h>

#include <atomic>
#include <cstdint>
#include <cstring>

namespace
{
    struct WaylandState
    {
        wl_display* display = nullptr;
        wl_compositor* compositor = nullptr;
        wl_shm* shm = nullptr;
        xdg_wm_base* xdgWmBase = nullptr;
        zxdg_decoration_manager_v1* decoManager = nullptr;
        wl_seat* seat = nullptr;
        wl_keyboard* keyboard = nullptr;
        std::atomic<uint32_t> openWindowCount{ 0 };
        bool running = false;
        bool displayConnected = false;
    };

    WaylandState g_state;

    void xdgWmBasePing(void*, xdg_wm_base* shell, uint32_t serial)
    {
        xdg_wm_base_pong(shell, serial);
    }

    const xdg_wm_base_listener g_xdgWmBaseListener = {
        .ping = xdgWmBasePing,
    };

    // ---- wl_keyboard listener ----
    void keyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t, uint32_t) {}
    void keyboardEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
    void keyboardLeave(void*, wl_keyboard*, uint32_t, wl_surface*) {}

    void keyboardKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state)
    {
        if (key == 1 && state == WL_KEYBOARD_KEY_STATE_PRESSED)
        {
            LWS::internal::decrementWindowCount();
        }
    }

    void keyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t) {}
    void keyboardRepeatInfo(void*, wl_keyboard*, int32_t, int32_t) {}

    const wl_keyboard_listener g_keyboardListener = {
        .keymap = keyboardKeymap,
        .enter = keyboardEnter,
        .leave = keyboardLeave,
        .key = keyboardKey,
        .modifiers = keyboardModifiers,
        .repeat_info = keyboardRepeatInfo,
    };

    // ---- wl_seat listener ----
    void seatCapabilities(void* data, wl_seat* seat, uint32_t caps)
    {
        auto* state = static_cast<WaylandState*>(data);
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !state->keyboard)
        {
            state->keyboard = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(state->keyboard, &g_keyboardListener, nullptr);
        }
        else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && state->keyboard)
        {
            wl_keyboard_destroy(state->keyboard);
            state->keyboard = nullptr;
        }
    }

    void seatName(void*, wl_seat*, const char*) {}

    const wl_seat_listener g_seatListener = {
        .capabilities = seatCapabilities,
        .name = seatName,
    };

    // ---- wl_registry listener ----
    void registryGlobal(void* data, wl_registry* registry, uint32_t name,
        const char* interface, uint32_t)
    {
        auto* state = static_cast<WaylandState*>(data);

        if (std::strcmp(interface, wl_compositor_interface.name) == 0)
        {
            state->compositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, 4));
        }
        else if (std::strcmp(interface, wl_shm_interface.name) == 0)
        {
            state->shm = static_cast<wl_shm*>(
                wl_registry_bind(registry, name, &wl_shm_interface, 1));
        }
        else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
        {
            state->xdgWmBase = static_cast<xdg_wm_base*>(
                wl_registry_bind(registry, name, &xdg_wm_base_interface, 1));
            xdg_wm_base_add_listener(state->xdgWmBase, &g_xdgWmBaseListener, nullptr);
        }
        else if (std::strcmp(interface, wl_seat_interface.name) == 0)
        {
            state->seat = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface, 7));
            wl_seat_add_listener(state->seat, &g_seatListener, state);
        }
        else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        {
            state->decoManager = static_cast<zxdg_decoration_manager_v1*>(
                wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1));
        }
    }

    void registryGlobalRemove(void*, wl_registry*, uint32_t) {}

    const wl_registry_listener g_registryListener = {
        .global = registryGlobal,
        .global_remove = registryGlobalRemove,
    };
}

namespace LWS::internal
{
    wl_display* getWlDisplay() { return g_state.display; }
    wl_compositor* getWlCompositor() { return g_state.compositor; }
    wl_shm* getWlShm() { return g_state.shm; }
    xdg_wm_base* getXdgWmBase() { return g_state.xdgWmBase; }
    zxdg_decoration_manager_v1* getDecoManager() { return g_state.decoManager; }

    void incrementWindowCount() { g_state.openWindowCount.fetch_add(1); }
    void decrementWindowCount()
    {
        if (g_state.openWindowCount.fetch_sub(1) == 1)
        {
            g_state.running = false;
        }
    }

    bool isDisplayConnected() { return g_state.displayConnected; }

    void invalidateDisplay() { g_state.displayConnected = false; }
}

namespace LWS::Platform
{
    namespace
    {
        uint32_t g_initCount = 0;
    }

    Result init()
    {
        if (g_initCount != 0)
        {
            ++g_initCount;
            return Result::Success;
        }

        g_state.display = wl_display_connect(nullptr);
        if (!g_state.display)
        {
            return Result::Failure;
        }
        g_state.displayConnected = true;

        wl_registry* registry = wl_display_get_registry(g_state.display);
        wl_registry_add_listener(registry, &g_registryListener, &g_state);

        wl_display_roundtrip(g_state.display);

        if (!g_state.compositor || !g_state.shm || !g_state.xdgWmBase)
        {
            wl_display_disconnect(g_state.display);
            g_state.display = nullptr;
            return Result::Failure;
        }

        g_initCount = 1;
        return Result::Success;
    }

    void shutdown()
    {
        if (g_initCount == 0)
            return;

        --g_initCount;
        if (g_initCount == 0)
        {
            if (g_state.keyboard)
            {
                wl_keyboard_destroy(g_state.keyboard);
                g_state.keyboard = nullptr;
            }
            if (g_state.seat)
            {
                wl_seat_destroy(g_state.seat);
                g_state.seat = nullptr;
            }
            if (g_state.decoManager)
            {
                zxdg_decoration_manager_v1_destroy(g_state.decoManager);
                g_state.decoManager = nullptr;
            }
            if (g_state.xdgWmBase)
            {
                xdg_wm_base_destroy(g_state.xdgWmBase);
                g_state.xdgWmBase = nullptr;
            }
            if (g_state.shm)
            {
                wl_shm_destroy(g_state.shm);
                g_state.shm = nullptr;
            }
            if (g_state.compositor)
            {
                wl_compositor_destroy(g_state.compositor);
                g_state.compositor = nullptr;
            }
            if (g_state.display)
            {
                wl_display_flush(g_state.display);
                g_state.displayConnected = false;
                wl_display_disconnect(g_state.display);
                g_state.display = nullptr;
            }
        }
    }

    bool isInitialized()
    {
        return g_initCount != 0;
    }

    void runMessageLoop()
    {
        g_state.running = true;

        while (g_state.running && g_state.openWindowCount.load() > 0)
        {
            wl_display_flush(g_state.display);

            struct pollfd pfd;
            pfd.fd = wl_display_get_fd(g_state.display);
            pfd.events = POLLIN;

            int ret = poll(&pfd, 1, -1);
            if (ret < 0)
                break;

            wl_display_dispatch(g_state.display);
        }
    }

    bool processMessages()
    {
        if (g_state.openWindowCount.load() == 0)
            return true;

        wl_display_flush(g_state.display);

        struct pollfd pfd;
        pfd.fd = wl_display_get_fd(g_state.display);
        pfd.events = POLLIN;

        int ret = poll(&pfd, 1, 0);
        if (ret > 0)
            wl_display_dispatch_pending(g_state.display);

        return false;
    }

    bool isKeyPressed(KeyCode) { return false; }
    bool isKeyToggled(KeyCode) { return false; }
    Point getMousePosition() { return {}; }
    void moveMouse(Point) {}
    void browseToFile(const std::filesystem::path&) {}
    void refreshMonitors() {}
    MonitorDesc getMonitorInfo(Handle, bool) { return {}; }
    MonitorDesc getPrimaryMonitor(bool) { return {}; }
    Rect getBoundingMonitorArea() { return {}; }
}
#endif
