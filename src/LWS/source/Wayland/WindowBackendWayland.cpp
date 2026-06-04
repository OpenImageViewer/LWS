// Wayland backend stub implementation.
// All methods return Result::NotSupported or do nothing.
// Replace with real wl_* API calls once wayland-client.h is available at build time.
#include <LWS/Wayland/WindowBackendWayland.hpp>

#ifdef LWS_PLATFORM_WAYLAND

#include <utility>
#include <vector>

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

        // TODO: wl_compositor_create_surface(g_wlCompositor) → fWlSurface
        // TODO: xdg_wm_base_get_xdg_surface(g_xdgWmBase, fWlSurface) → fXdgSurface
        // TODO: xdg_surface_get_toplevel(fXdgSurface) → fXdgToplevel
        // TODO: xdg_toplevel_set_title(fXdgToplevel, fTitle.c_str())
        // TODO: wl_surface_commit(fWlSurface)
        return Result::NotSupported;
    }

    void WindowBackendWayland::destroy()
    {
        // TODO: xdg_toplevel_destroy(fXdgToplevel)
        // TODO: xdg_surface_destroy(fXdgSurface)
        // TODO: wl_surface_destroy(fWlSurface)
        fXdgToplevel = nullptr;
        fXdgSurface = nullptr;
        fWlSurface = nullptr;
    }

    void WindowBackendWayland::show()
    {
        fVisible = true;
        // TODO: unset_minimized on xdg_toplevel; wl_surface_commit
    }

    void WindowBackendWayland::hide()
    {
        fVisible = false;
        // TODO: xdg_toplevel_set_minimized(fXdgToplevel)
    }

    bool WindowBackendWayland::getVisible() const
    {
        return fVisible;
    }

    void WindowBackendWayland::setDisplayState(WindowDisplayState state)
    {
        fDisplayState = state;
        // TODO: map to xdg_toplevel_set_minimized / set_maximized / unset_maximized
    }

    WindowDisplayState WindowBackendWayland::getDisplayState() const
    {
        return fDisplayState;
    }

    void WindowBackendWayland::setTitle(const LWS::string_type& title)
    {
        fTitle = title;
        // TODO: xdg_toplevel_set_title(fXdgToplevel, utf8_title.c_str())
    }

    LWS::string_type WindowBackendWayland::getTitle() const
    {
        return fTitle;
    }

    void WindowBackendWayland::setWindowIcon(const std::filesystem::path&)
    {
        // No standard Wayland protocol for window icons.
        // Desktop integration via .desktop file application icon.
    }

    void WindowBackendWayland::setPosition(Point)
    {
        // Wayland does not expose a window position API.
        // Position is managed entirely by the compositor.
    }

    Point WindowBackendWayland::getPosition() const
    {
        // Not available on Wayland — compositor controls placement.
        return { 0, 0 };
    }

    void WindowBackendWayland::setSize(Size sz)
    {
        fSize = sz;
        // TODO: xdg_toplevel configure response (size negotiation via configure event)
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
        // Position ignored — compositor-controlled on Wayland.
    }

    WindowPlacement WindowBackendWayland::getPlacement() const
    {
        return { { 0, 0 }, fSize, fDisplayState };
    }

    void WindowBackendWayland::setMinMaxSize(Size minSize, Size maxSize)
    {
        fMinSize = minSize;
        fMaxSize = maxSize;
        // TODO: xdg_toplevel_set_min_size(fXdgToplevel, minSize.x, minSize.y)
        // TODO: xdg_toplevel_set_max_size(fXdgToplevel, maxSize.x, maxSize.y)
    }

    Size WindowBackendWayland::getMinSize() const { return fMinSize; }
    Size WindowBackendWayland::getMaxSize() const { return fMaxSize; }

    void WindowBackendWayland::setWindowStyles(WindowStyle styles, bool enable)
    {
        auto current = std::to_underlying(fWindowStyles);
        fWindowStyles = static_cast<WindowStyle>(enable
            ? (current | std::to_underlying(styles))
            : (current & ~std::to_underlying(styles)));
        // Wayland compositor controls window decorations; only limited style hints available
        // via the xdg-decoration protocol (prefer server-side vs client-side decorations).
    }

    WindowStyle WindowBackendWayland::getWindowStyles() const { return fWindowStyles; }

    void WindowBackendWayland::setForeground()
    {
        // No Wayland API to programmatically raise or activate a window.
        // Activation is compositor-initiated only (e.g. xdg_activation_v1 token).
    }

    void WindowBackendWayland::setFocus()
    {
        // Same as setForeground — compositor-controlled.
    }

    bool WindowBackendWayland::isInFocus() const
    {
        // TODO: track via wl_keyboard.enter / wl_keyboard.leave events
        return false;
    }

    void WindowBackendWayland::setAlwaysOnTop(bool onTop)
    {
        fAlwaysOnTop = onTop;
        // TODO: zwlr_layer_shell_v1 (layer surface) — requires compositor extension support
    }

    bool WindowBackendWayland::getAlwaysOnTop() const { return fAlwaysOnTop; }

    void WindowBackendWayland::setTransparent(bool transparent)
    {
        fTransparent = transparent;
        // wl_surface alpha channel is always available — controlled via the pixel buffer content.
    }

    bool WindowBackendWayland::getTransparent() const { return fTransparent; }

    void WindowBackendWayland::setBackgroundColor(LLUtils::Color color)
    {
        fBackgroundColor = color;
        // TODO: fill wl_shm buffer with background color when no app content is rendered
    }

    LLUtils::Color WindowBackendWayland::getBackgroundColor() const { return fBackgroundColor; }

    void WindowBackendWayland::setEraseBackground(bool erase) { fEraseBackground = erase; }
    bool WindowBackendWayland::getEraseBackground() const { return fEraseBackground; }

    void WindowBackendWayland::setFullScreenState(FullScreenState state)
    {
        fFullScreenState = state;
        fFullScreen = (state != FullScreenState::None && state != FullScreenState::Windowed);
        // TODO: xdg_toplevel_set_fullscreen(fXdgToplevel, wl_output) / unset_fullscreen
    }

    FullScreenState WindowBackendWayland::getFullScreenState() const { return fFullScreenState; }
    bool WindowBackendWayland::isFullScreen() const { return fFullScreen; }

    void WindowBackendWayland::toggleFullScreen(bool)
    {
        setFullScreenState(fFullScreen ? FullScreenState::Windowed : FullScreenState::SingleScreen);
    }

    bool WindowBackendWayland::isMouseInClientRect() const
    {
        // TODO: track via wl_pointer.enter / wl_pointer.motion / wl_pointer.leave
        return false;
    }

    bool WindowBackendWayland::isUnderMouseCursor() const { return isMouseInClientRect(); }

    Point WindowBackendWayland::getMousePosition() const
    {
        // TODO: last known pointer position from wl_pointer.motion events
        return { 0, 0 };
    }

    void WindowBackendWayland::setLockMouseToWindowMode(LockMouseToWindowMode mode)
    {
        fLockMode = mode;
        // TODO: zwp_pointer_constraints_v1.lock_pointer (zwp-pointer-constraints-unstable-v1)
    }

    LockMouseToWindowMode WindowBackendWayland::getLockMouseToWindowMode() const { return fLockMode; }

    void WindowBackendWayland::setDoubleClickMode(DoubleClickMode mode)
    {
        fDoubleClickMode = mode;
        // Wayland compositor controls window decorations; non-client double-click behaviour
        // is not available via any standard protocol. ClientArea double-clicks are fired via
        // the normal wl_pointer button event sequence detected by the application.
    }

    DoubleClickMode WindowBackendWayland::getDoubleClickMode() const { return fDoubleClickMode; }

    void WindowBackendWayland::setCursor(std::shared_ptr<ICursorBackend>)
    {
        // TODO: cast to CursorBackendWayland; call wl_pointer.set_cursor with wl_surface
    }

    void WindowBackendWayland::setParent(IWindowBackend*)
    {
        // TODO: xdg_toplevel_set_parent(fXdgToplevel, parent->fXdgToplevel)
    }

    void WindowBackendWayland::enableDragAndDrop(bool)
    {
        // TODO: wl_data_device_manager + wl_data_device offer/drop protocol
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
        // TODO: cast to Wayland-specific raw event struct and process
    }

    Handle WindowBackendWayland::getHandle() const
    {
        return reinterpret_cast<Handle>(fWlSurface);
    }
}

#endif // LWS_PLATFORM_WAYLAND
