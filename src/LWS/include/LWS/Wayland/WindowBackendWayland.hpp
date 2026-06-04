#pragma once
#include <utility>
#include <vector>

#include <LWS/interfaces/backends.hpp>

namespace LWS
{
    /// Wayland implementation of IWindowBackend.
    /// All methods are currently stubs — to be implemented in a future commit
    /// when a Wayland compositor is available at build time.
    ///
    /// Protocol mapping:
    ///   create()           → wl_surface + xdg_toplevel_surface (xdg-shell protocol)
    ///   destroy()          → xdg_toplevel_destroy + wl_surface_destroy
    ///   show/hide          → xdg_toplevel_set_minimized / unset_minimized
    ///   setTitle()         → xdg_toplevel_set_title()
    ///   setPosition()      → no direct Wayland API (compositor-controlled placement)
    ///   setMinMaxSize()    → xdg_toplevel_set_min_size / set_max_size
    ///   setFullScreenState → xdg_toplevel_set_fullscreen / unset_fullscreen
    ///   setAlwaysOnTop()   → zwlr_layer_shell_v1 layer surface (extension; not universal)
    ///   setTransparent()   → compositor alpha channel on wl_surface (always available)
    ///   setCursor()        → wl_cursor_theme + wl_pointer.set_cursor
    ///   enableDragAndDrop  → wl_data_device offer/drop (wl_data_device_manager protocol)
    ///   addListener()      → wl_event_queue + epoll on wl_display_get_fd()
    class WindowBackendWayland : public IWindowBackend
    {
    public:
        WindowBackendWayland() = default;
        ~WindowBackendWayland() override;

        // IWindowBackend
        Result create(const WindowConfig& config) override;
        void destroy() override;
        void show() override;
        void hide() override;
        bool getVisible() const override;
        void setDisplayState(WindowDisplayState state) override;
        WindowDisplayState getDisplayState() const override;
        void setTitle(const LWS::string_type& title) override;
        LWS::string_type getTitle() const override;
        void setWindowIcon(const std::filesystem::path& iconPath) override;
        void setPosition(Point pos) override;
        Point getPosition() const override;
        void setSize(Size sz) override;
        Size getClientSize() const override;
        Rect getClientRect() const override;
        Size getWindowSize() const override;
        void setPlacement(const WindowPlacement& placement) override;
        WindowPlacement getPlacement() const override;
        void setMinMaxSize(Size minSize, Size maxSize) override;
        Size getMinSize() const override;
        Size getMaxSize() const override;
        void setWindowStyles(WindowStyle styles, bool enable) override;
        WindowStyle getWindowStyles() const override;
        void setForeground() override;
        void setFocus() override;
        bool isInFocus() const override;
        void setAlwaysOnTop(bool onTop) override;
        bool getAlwaysOnTop() const override;
        void setTransparent(bool transparent) override;
        bool getTransparent() const override;
        void setBackgroundColor(LLUtils::Color color) override;
        LLUtils::Color getBackgroundColor() const override;
        void setEraseBackground(bool erase) override;
        bool getEraseBackground() const override;
        void setFullScreenState(FullScreenState state) override;
        FullScreenState getFullScreenState() const override;
        bool isFullScreen() const override;
        void toggleFullScreen(bool multiMonitor = false) override;
        bool isMouseInClientRect() const override;
        bool isUnderMouseCursor() const override;
        Point getMousePosition() const override;
        void setLockMouseToWindowMode(LockMouseToWindowMode mode) override;
        LockMouseToWindowMode getLockMouseToWindowMode() const override;
        void setDoubleClickMode(DoubleClickMode mode) override;
        DoubleClickMode getDoubleClickMode() const override;
        void setCursor(std::shared_ptr<ICursorBackend> cursor) override;
        void setParent(IWindowBackend* parent) override;
        void enableDragAndDrop(bool enable) override;
        EventListenerToken addListener(EventCallback cb) override;
        void removeListener(EventListenerToken token) override;
        void injectRawEvent(void* platformEvent) override;
        Handle getHandle() const override;
        BackendId backend() const override { return BackendId::Wayland; }

    private:
        // Wayland surface handles (opaque void* to avoid including wayland-client.h here)
        void* fWlSurface = nullptr;      // wl_surface*
        void* fXdgSurface = nullptr;     // xdg_surface*
        void* fXdgToplevel = nullptr;    // xdg_toplevel*

        LWS::string_type fTitle;
        Size fSize = { 800, 600 };
        Size fMinSize = { 0, 0 };
        Size fMaxSize = { 0, 0 };
        bool fVisible = false;
        bool fAlwaysOnTop = false;
        bool fTransparent = false;
        bool fEraseBackground = true;
        bool fFullScreen = false;
        LLUtils::Color fBackgroundColor;
        WindowStyle fWindowStyles = WindowStyle::Caption;
        WindowDisplayState fDisplayState = WindowDisplayState::Restored;
        FullScreenState fFullScreenState = FullScreenState::None;
        LockMouseToWindowMode fLockMode = LockMouseToWindowMode::NoLock;
        DoubleClickMode fDoubleClickMode = DoubleClickMode::NotSet;

        uint64_t fNextListenerToken = 1;
        std::vector<std::pair<EventListenerToken, EventCallback>> fListeners;
    };
}
