#include <LWS/Wayland/WindowBackendWayland.hpp>
#include "internal/WaylandPlatformState.hpp"

#ifdef LWS_PLATFORM_WAYLAND

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <xdg-shell-client-protocol.h>
#include <xdg-decoration-unstable-v1-client-protocol.h>
#include <LLUtils/Colors.h>

#include <cerrno>
#include <cstring>
#include <utility>
#include <vector>

namespace
{
    void xdgSurfaceConfigure(void* data, xdg_surface* surface, uint32_t serial)
    {
        xdg_surface_ack_configure(surface, serial);
        auto* self = static_cast<LWS::WindowBackendWayland*>(data);
        self->markConfigured();
        self->commitBuffer();
    }

    const xdg_surface_listener g_xdgSurfaceListener = {
        .configure = xdgSurfaceConfigure,
    };

    void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, [[maybe_unused]] wl_array*)
    {
        auto* self = static_cast<LWS::WindowBackendWayland*>(data);
        if (width > 0 && height > 0)
        {
            self->setSize({ width, height });
        }
    }

    void toplevelClose(void*, xdg_toplevel*)
    {
        LWS::internal::decrementWindowCount();
    }

    void toplevelConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t)
    {
        // Advisory bounds from the compositor — ignored for now.
    }

    void toplevelWmCapabilities(void*, xdg_toplevel*, wl_array*)
    {
        // Advertised compositor capabilities — ignored for now.
    }

    const xdg_toplevel_listener g_xdgToplevelListener = {
        .configure = toplevelConfigure,
        .close = toplevelClose,
        .configure_bounds = toplevelConfigureBounds,
        .wm_capabilities = toplevelWmCapabilities,
    };

    // ---- zxdg_toplevel_decoration_v1 listener ----
    void decoConfigure(void*, zxdg_toplevel_decoration_v1*, uint32_t) {}

    const zxdg_toplevel_decoration_v1_listener g_decoListener = {
        .configure = decoConfigure,
    };

    // Scale from LLUtils Color (ARGB) to a Wayland-compatible ARGB8888 uint32_t.
    uint32_t colorToArgb8888(LLUtils::Color color)
    {
        return (static_cast<uint32_t>(color.A()) << 24) |
            (static_cast<uint32_t>(color.R()) << 16) |
            (static_cast<uint32_t>(color.G()) << 8) |
            static_cast<uint32_t>(color.B());
    }
}

namespace LWS
{
    WindowBackendWayland::~WindowBackendWayland()
    {
        destroy();
    }

    Result WindowBackendWayland::create(const WindowConfig& config)
    {
        fTitle = config.title;
        fSize = config.size;
        fMinSize = config.minSize;
        fMaxSize = config.maxSize;
        fVisible = config.visible;
        fAlwaysOnTop = config.alwaysOnTop;
        fTransparent = config.transparent;
        fEraseBackground = config.eraseBackground;
        fWindowStyles = config.styles;
        fDisplayState = config.displayState;
        fBackgroundColor = LLUtils::Colors::Black;

        wl_compositor* compositor = internal::getWlCompositor();
        xdg_wm_base* xdgWmBase = internal::getXdgWmBase();
        if (!compositor || !xdgWmBase)
        {
            return Result::Failure;
        }

        // Create wl_surface
        fWlSurface = wl_compositor_create_surface(compositor);
        if (!fWlSurface)
        {
            return Result::Failure;
        }

        // Create xdg_surface from wl_surface
        fXdgSurface = xdg_wm_base_get_xdg_surface(xdgWmBase, fWlSurface);
        if (!fXdgSurface)
        {
            wl_surface_destroy(fWlSurface);
            fWlSurface = nullptr;
            return Result::Failure;
        }
        xdg_surface_add_listener(fXdgSurface, &g_xdgSurfaceListener, this);

        // Create xdg_toplevel from xdg_surface
        fXdgToplevel = xdg_surface_get_toplevel(fXdgSurface);
        if (!fXdgToplevel)
        {
            xdg_surface_destroy(fXdgSurface);
            fXdgSurface = nullptr;
            wl_surface_destroy(fWlSurface);
            fWlSurface = nullptr;
            return Result::Failure;
        }
        xdg_toplevel_add_listener(fXdgToplevel, &g_xdgToplevelListener, this);

        // Request server-side decorations
        zxdg_decoration_manager_v1* decoManager = internal::getDecoManager();
        if (decoManager)
        {
            fDecoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
                decoManager, fXdgToplevel);
            zxdg_toplevel_decoration_v1_add_listener(fDecoration, &g_decoListener, this);
            zxdg_toplevel_decoration_v1_set_mode(fDecoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        }

        // Set the window title (convert from native string to UTF-8)
        xdg_toplevel_set_title(fXdgToplevel, fTitle.c_str());

        // Set minimum/maximum size hints
        if (fMinSize.x > 0 && fMinSize.y > 0)
        {
            xdg_toplevel_set_min_size(fXdgToplevel, fMinSize.x, fMinSize.y);
        }
        if (fMaxSize.x > 0 && fMaxSize.y > 0)
        {
            xdg_toplevel_set_max_size(fXdgToplevel, fMaxSize.x, fMaxSize.y);
        }

        // Commit the surface to trigger the initial configure
        wl_surface_commit(fWlSurface);

        // Track open windows for message loop auto-exit
        fWindowCounted = true;
        internal::incrementWindowCount();

        return Result::Success;
    }

    void WindowBackendWayland::destroy()
    {
        destroyShmBuffer();

        // Only send protocol destroy requests if the display connection is still alive.
        // shutdown() invalidates the display before disconnecting; if the window destructor
        // runs after that, we must not touch any wl_* or xdg_* objects.
        if (internal::isDisplayConnected())
        {
            if (fDecoration)
            {
                zxdg_toplevel_decoration_v1_destroy(fDecoration);
                fDecoration = nullptr;
            }
            if (fXdgToplevel)
            {
                xdg_toplevel_destroy(fXdgToplevel);
                fXdgToplevel = nullptr;
            }
            if (fXdgSurface)
            {
                xdg_surface_destroy(fXdgSurface);
                fXdgSurface = nullptr;
            }
            if (fWlSurface)
            {
                wl_surface_destroy(fWlSurface);
                fWlSurface = nullptr;
            }
        }
        else
        {
            fDecoration = nullptr;
            fXdgToplevel = nullptr;
            fXdgSurface = nullptr;
            fWlSurface = nullptr;
        }

        // Decrement open window count — may trigger message loop exit
        if (fWindowCounted)
        {
            fWindowCounted = false;
            internal::decrementWindowCount();
        }
    }

    void WindowBackendWayland::show()
    {
        fVisible = true;
    }

    void WindowBackendWayland::hide()
    {
        fVisible = false;
        if (fXdgToplevel)
        {
            xdg_toplevel_set_minimized(fXdgToplevel);
        }
    }

    bool WindowBackendWayland::getVisible() const
    {
        return fVisible;
    }

    void WindowBackendWayland::setDisplayState(WindowDisplayState state)
    {
        fDisplayState = state;
        if (!fXdgToplevel)
            return;

        switch (state)
        {
        case WindowDisplayState::Minimized:
            xdg_toplevel_set_minimized(fXdgToplevel);
            break;
        case WindowDisplayState::Maximized:
            xdg_toplevel_set_maximized(fXdgToplevel);
            break;
        case WindowDisplayState::Restored:
            xdg_toplevel_unset_maximized(fXdgToplevel);
            break;
        default:
            break;
        }
    }

    WindowDisplayState WindowBackendWayland::getDisplayState() const
    {
        return fDisplayState;
    }

    void WindowBackendWayland::setTitle(const LWS::string_type& title)
    {
        fTitle = title;
        if (fXdgToplevel)
        {
            xdg_toplevel_set_title(fXdgToplevel, fTitle.c_str());
        }
    }

    LWS::string_type WindowBackendWayland::getTitle() const
    {
        return fTitle;
    }

    void WindowBackendWayland::setWindowIcon(const std::filesystem::path&)
    {
        // No standard Wayland protocol for window icons.
    }

    void WindowBackendWayland::setPosition(Point)
    {
        // Wayland does not expose a window position API.
    }

    Point WindowBackendWayland::getPosition() const
    {
        return { 0, 0 };
    }

    void WindowBackendWayland::setSize(Size sz)
    {
        fSize = sz;
    }

    Size WindowBackendWayland::getClientSize() const
    {
        return fSize;
    }

    Rect WindowBackendWayland::getClientRect() const
    {
        return { { 0, 0 }, fSize };
    }

    Size WindowBackendWayland::getWindowSize() const
    {
        return fSize;
    }

    void WindowBackendWayland::setPlacement(const WindowPlacement& p)
    {
        fSize = p.size;
        fDisplayState = p.displayState;
    }

    WindowPlacement WindowBackendWayland::getPlacement() const
    {
        return { { 0, 0 }, fSize, fDisplayState };
    }

    void WindowBackendWayland::setMinMaxSize(Size minSize, Size maxSize)
    {
        fMinSize = minSize;
        fMaxSize = maxSize;
        if (fXdgToplevel)
        {
            if (fMinSize.x > 0 && fMinSize.y > 0)
                xdg_toplevel_set_min_size(fXdgToplevel, fMinSize.x, fMinSize.y);
            if (fMaxSize.x > 0 && fMaxSize.y > 0)
                xdg_toplevel_set_max_size(fXdgToplevel, fMaxSize.x, fMaxSize.y);
        }
    }

    Size WindowBackendWayland::getMinSize() const { return fMinSize; }
    Size WindowBackendWayland::getMaxSize() const { return fMaxSize; }

    void WindowBackendWayland::setWindowStyles(WindowStyle styles, bool enable)
    {
        auto current = std::to_underlying(fWindowStyles);
        fWindowStyles = static_cast<WindowStyle>(enable
            ? (current | std::to_underlying(styles))
            : (current & ~std::to_underlying(styles)));
    }

    WindowStyle WindowBackendWayland::getWindowStyles() const { return fWindowStyles; }

    void WindowBackendWayland::setForeground()
    {
        // No Wayland API to programmatically raise or activate a window.
    }

    void WindowBackendWayland::setFocus()
    {
        // Same as setForeground — compositor-controlled.
    }

    bool WindowBackendWayland::isInFocus() const
    {
        return false;
    }

    void WindowBackendWayland::setAlwaysOnTop(bool onTop)
    {
        fAlwaysOnTop = onTop;
    }

    bool WindowBackendWayland::getAlwaysOnTop() const { return fAlwaysOnTop; }

    void WindowBackendWayland::setTransparent(bool transparent)
    {
        fTransparent = transparent;
    }

    bool WindowBackendWayland::getTransparent() const { return fTransparent; }

    void WindowBackendWayland::setBackgroundColor(LLUtils::Color color)
    {
        fBackgroundColor = color;
        if (fWlSurface && fEraseBackground && internal::isDisplayConnected())
        {
            destroyShmBuffer();
            initShmBuffer();
            commitBuffer();
        }
    }

    LLUtils::Color WindowBackendWayland::getBackgroundColor() const { return fBackgroundColor; }

    void WindowBackendWayland::setEraseBackground(bool erase)
    {
        fEraseBackground = erase;
    }

    bool WindowBackendWayland::getEraseBackground() const { return fEraseBackground; }

    void WindowBackendWayland::setFullScreenState(FullScreenState state)
    {
        fFullScreenState = state;
        fFullScreen = (state != FullScreenState::None && state != FullScreenState::Windowed);
        if (!fXdgToplevel)
            return;

        if (fFullScreen)
        {
            xdg_toplevel_set_fullscreen(fXdgToplevel, nullptr);
        }
        else
        {
            xdg_toplevel_unset_fullscreen(fXdgToplevel);
        }
    }

    FullScreenState WindowBackendWayland::getFullScreenState() const { return fFullScreenState; }
    bool WindowBackendWayland::isFullScreen() const { return fFullScreen; }

    void WindowBackendWayland::toggleFullScreen(bool)
    {
        setFullScreenState(fFullScreen ? FullScreenState::Windowed : FullScreenState::SingleScreen);
    }

    bool WindowBackendWayland::isMouseInClientRect() const
    {
        return false;
    }

    bool WindowBackendWayland::isUnderMouseCursor() const { return isMouseInClientRect(); }

    Point WindowBackendWayland::getMousePosition() const
    {
        return { 0, 0 };
    }

    void WindowBackendWayland::setLockMouseToWindowMode(LockMouseToWindowMode mode)
    {
        fLockMode = mode;
    }

    LockMouseToWindowMode WindowBackendWayland::getLockMouseToWindowMode() const { return fLockMode; }

    void WindowBackendWayland::setDoubleClickMode(DoubleClickMode mode)
    {
        fDoubleClickMode = mode;
    }

    DoubleClickMode WindowBackendWayland::getDoubleClickMode() const { return fDoubleClickMode; }

    void WindowBackendWayland::setCursor(std::shared_ptr<ICursorBackend>)
    {
        // TODO: cast to CursorBackendWayland; call wl_pointer.set_cursor with wl_surface
    }

    void WindowBackendWayland::setParent(IWindowBackend*)
    {
        // TODO: xdg_toplevel_set_parent
    }

    Result WindowBackendWayland::enableDragAndDrop(bool)
    {
        return Result::NotSupported;
    }

    EventListenerToken WindowBackendWayland::addListener(EventCallback cb)
    {
        EventListenerToken token = fNextListenerToken++;
        fListeners.emplace_back(token, std::move(cb));
        return token;
    }

    void WindowBackendWayland::removeListener(EventListenerToken token)
    {
        std::erase_if(fListeners, [token](const auto& pair) { return pair.first == token; });
    }

    void WindowBackendWayland::injectRawEvent(void*)
    {
    }

    Handle WindowBackendWayland::getHandle() const
    {
        return reinterpret_cast<Handle>(fWlSurface);
    }

    // ---- SHM buffer management ----

    void WindowBackendWayland::initShmBuffer()
    {
        wl_shm* shm = internal::getWlShm();
        if (!shm)
            return;

        uint32_t stride = fSize.x * 4;
        fShmSize = stride * fSize.y;

        // Create anonymous file for shared memory
        fShmFd = memfd_create("lws-shm", MFD_CLOEXEC);
        if (fShmFd < 0)
            return;

        // Set the file size
        if (ftruncate(fShmFd, fShmSize) < 0)
        {
            close(fShmFd);
            fShmFd = -1;
            return;
        }

        // Memory-map the file
        fShmData = mmap(nullptr, fShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fShmFd, 0);
        if (fShmData == MAP_FAILED)
        {
            close(fShmFd);
            fShmFd = -1;
            fShmData = nullptr;
            return;
        }

        // Fill the buffer with the background color
        uint32_t* pixels = static_cast<uint32_t*>(fShmData);
        uint32_t argb = colorToArgb8888(fBackgroundColor);
        for (uint32_t i = 0; i < fSize.x * fSize.y; ++i)
        {
            pixels[i] = argb;
        }

        // Create wl_shm_pool and wl_buffer
        wl_shm_pool* pool = wl_shm_create_pool(shm, fShmFd, fShmSize);
        fWlBuffer = wl_shm_pool_create_buffer(pool, 0, fSize.x, fSize.y, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
    }

    void WindowBackendWayland::destroyShmBuffer()
    {
        if (fWlBuffer)
        {
            if (internal::isDisplayConnected())
                wl_buffer_destroy(fWlBuffer);
            fWlBuffer = nullptr;
        }
        if (fShmData && fShmData != MAP_FAILED)
        {
            munmap(fShmData, fShmSize);
            fShmData = nullptr;
        }
        if (fShmFd >= 0)
        {
            close(fShmFd);
            fShmFd = -1;
        }
        fShmSize = 0;
    }

    void WindowBackendWayland::commitBuffer()
    {
        if (!fWlSurface)
            return;

        if (fEraseBackground && !fWlBuffer)
        {
            initShmBuffer();
        }

        if (!fWlBuffer || !fConfigured)
            return;

        wl_surface_attach(fWlSurface, fWlBuffer, 0, 0);
        wl_surface_damage_buffer(fWlSurface, 0, 0, fSize.x, fSize.y);
        wl_surface_commit(fWlSurface);
    }
}

#endif // LWS_PLATFORM_WAYLAND
