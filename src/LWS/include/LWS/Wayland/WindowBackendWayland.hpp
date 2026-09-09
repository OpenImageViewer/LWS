#pragma once
#include <optional>
#include <utility>
#include <vector>

#include <LWS/interfaces/backends.hpp>

namespace LWS
{
    namespace internal
    {
        enum class WaylandCaptionMode;
        enum class WaylandDecorationMode;
        class WaylandDragAndDropController;
        struct WaylandFrameHit;
        enum class WaylandResizeEdge : uint32_t;
        enum class WaylandSurfaceRole;
    }  // namespace internal

    /// Wayland implementation of IWindowBackend.
    /// All methods are currently stubs — to be implemented in a future commit
    /// when a Wayland compositor is available at build time.
    ///
    /// Protocol mapping:
    ///   create()           → wl_surface + xdg_toplevel or wl_subsurface for ChildWindow
    ///   destroy()          → destroy the assigned role, then wl_surface
    ///   show/hide          → attach or detach the surface buffer
    ///   setTitle()         → xdg_toplevel_set_title()
    ///   setPosition()      → wl_subsurface position; top-level placement is compositor-controlled
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

        WindowBackendWayland();
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
        Result setPointerLocked(bool locked) override;
        void setDoubleClickMode(DoubleClickMode mode) override;
        DoubleClickMode getDoubleClickMode() const override;
        void setCursor(std::shared_ptr<ICursorBackend> cursor) override;
        void setParent(IWindowBackend* parent) override;
        Result enableDragAndDrop(bool enable) override;
        EventListenerToken addListener(EventCallback cb) override;
        void removeListener(EventListenerToken token) override;
        void injectRawEvent(void* platformEvent) override;
        Result presentBitmap(const BitmapBuffer& bitmap) override;
        Handle getHandle() const override;
        BackendId backend() const override { return BackendId::Wayland; }

        void handlePointerEnter(Point position, internal::WaylandSurfaceRole surfaceRole);
        void handlePointerLeave();
        void handlePointerMotion(Point position, Point delta, internal::WaylandSurfaceRole surfaceRole);
        void handlePointerLockState(bool active);
        void handleRelativePointerMotion(double deltaX, double deltaY);
        void handlePointerButton(MouseButton button, bool pressed, Point position,
                                 internal::WaylandSurfaceRole surfaceRole, uint32_t time);
        void handlePointerWheel(int32_t delta, Point position);
        void handleKeyboardFocus(bool focused);
        void handleKey(KeyCode key, bool pressed, bool repeat = false);
        void handleToplevelConfigure(Size size, bool maximized, bool fullscreen);
        void setAppId(const std::string& appId);

      private:

        friend class CursorBackendWayland;
        friend class internal::WaylandDragAndDropController;

        class NativeState;
        std::unique_ptr<NativeState> fNativeState;
        std::shared_ptr<ICursorBackend> fCursor;
        WindowBackendWayland* fParentBackend = nullptr;
        std::vector<WindowBackendWayland*> fChildBackends;

        bool dispatchEvent(const AnyEvent& event);
        void applyCursor(CursorShape shape, bool visible);
        void applyClientCursor();
        void applyFrameCursor(const internal::WaylandFrameHit& hit);
        [[nodiscard]] bool beginResize(internal::WaylandResizeEdge edge);
        [[nodiscard]] bool canResize() const;
        [[nodiscard]] internal::WaylandCaptionMode captionMode() const;
        [[nodiscard]] Result createSubsurface(NativeState& nativeState);
        [[nodiscard]] Point contentOffset() const;
        [[nodiscard]] internal::WaylandDecorationMode decorationMode() const;
        [[nodiscard]] internal::WaylandFrameHit frameHit(Point position,
                                                         internal::WaylandSurfaceRole surfaceRole) const;
        [[nodiscard]] bool isChildWindow() const;
        void paintBackground();
        void presentPendingBitmap();
        void paintCaption();
        void updateChildInputRegions();
        void updateWindowGeometry();
        void updateSubsurfaceInputRegion();
        void updateSubsurfacePosition();
        [[nodiscard]] bool showsClientSideDecorations() const;
        [[nodiscard]] bool showsDetachedCaption() const;
        [[nodiscard]] bool isCaptionDoubleClick(uint32_t time, Point position);
        [[nodiscard]] WindowBackendWayland* dragDropTarget();

        // Wayland surface handles (opaque void* to avoid including wayland-client.h here)
        void* fWlSurface = nullptr;    // wl_surface*
        void* fXdgSurface = nullptr;   // xdg_surface*
        void* fXdgToplevel = nullptr;  // xdg_toplevel*

        LWS::string_type fTitle;
        Point fPosition{};
        Size fSize = {800, 600};
        std::optional<Size> fRestoredClientSize;
        Size fMinSize = {0, 0};
        Size fMaxSize = {0, 0};
        bool fVisible = false;
        bool fAlwaysOnTop = false;
        bool fTransparent = false;
        bool fEraseBackground = true;
        bool fFullScreen = false;
        bool fFocused = false;
        bool fMouseInside = false;
        bool fPointerLockRequested = false;
        bool fPointerLockActive = false;
        bool fDragAndDropEnabled = false;
        double fRelativeRemainderX = 0.0;
        double fRelativeRemainderY = 0.0;
        Point fMousePosition{};
        LLUtils::Color fBackgroundColor;
        WindowStyle fWindowStyles = WindowStyle::NoStyle;
        WindowDisplayState fDisplayState = WindowDisplayState::Restored;
        FullScreenState fFullScreenState = FullScreenState::None;
        LockMouseToWindowMode fLockMode = LockMouseToWindowMode::NoLock;
        DoubleClickMode fDoubleClickMode = DoubleClickMode::NotSet;
        bool fCaptionClickPending = false;
        uint32_t fLastCaptionClickTime = 0;
        Point fLastCaptionClickPosition{};

        uint64_t fNextListenerToken = 1;
        std::vector<std::pair<EventListenerToken, EventCallback>> fListeners;
    };
}  // namespace LWS
