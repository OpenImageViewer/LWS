#pragma once
#include <optional>
#include <utility>
#include <vector>

#include <LWS/interfaces/backends.hpp>

struct wl_display;
struct wl_output;

namespace LWS
{
    namespace internal
    {
        enum class WaylandCaptionMode;
        enum class WaylandDecorationMode;
        class WaylandDragAndDropController;
        class WaylandPlatformState;
        struct WaylandFrameHit;
        enum class WaylandResizeEdge : uint32_t;
        enum class WaylandSurfaceRole;
    }  // namespace internal

    /// Wayland window, surface resources, and input dispatch owned by one platform context.
    class WindowBackendWayland : public internal::IWindowBackend
    {
      public:

        WindowBackendWayland(Window& owner, internal::WaylandPlatformState& platform);
        ~WindowBackendWayland() override;

        // IWindowBackend
        Result create(const internal::NativeWindowConfig& config) override;
        void destroy() override;
        void show() override;
        void hide() override;
        bool getVisible() const override;
        bool isConfigured() const override;
        void setDisplayState(WindowShowState state) override;
        WindowShowState getDisplayState() const override;
        void setTitle(const LWS::string_type& title) override;
        LWS::string_type getTitle() const override;
        Result setWindowIcon(const BitmapBuffer* icon) override;
        void setPosition(Point pos) override;
        Point getPosition() const override;
        void setSize(Size sz) override;
        Size getClientSize() const override;
        Size getFramebufferSize() const override;
        void setPlacement(const internal::NativeWindowPlacement& placement) override;
        void setMinMaxSize(Size minSize, Size maxSize) override;
        Size getMinSize() const override;
        Size getMaxSize() const override;
        void setWindowStyles(WindowStyle styles, bool enable) override;
        WindowStyle getWindowStyles() const override;
        void setForeground() override;
        bool isInFocus() const override;
        void setAlwaysOnTop(bool onTop) override;
        bool getAlwaysOnTop() const override;
        void setTransparent(bool transparent) override;
        bool getTransparent() const override;
        void setBackgroundColor(LLUtils::Color color) override;
        void setEraseBackground(bool erase) override;
        bool getEraseBackground() const override;
        void setFullScreenState(internal::FullScreenState state) override;
        internal::FullScreenState getFullScreenState() const override;
        bool isMouseInClientRect() const override;
        Point getMousePosition() const override;
        void setLockMouseToWindowMode(internal::LockMouseToWindowMode mode) override;
        Result setPointerLocked(bool locked) override;
        void setCursor(std::shared_ptr<internal::ICursorBackend> cursor) override;
        void setParent(internal::IWindowBackend* parent) override;
        Result enableDragAndDrop(bool enable) override;
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
        void handleOutputChange(wl_output* output, bool removed);
        void setAppId(const std::string& appId);
        [[nodiscard]] void* surface() const;
        [[nodiscard]] wl_display* display() const;
        [[nodiscard]] double contentScale() const;

      private:

        friend class CursorBackendWayland;
        friend class internal::WaylandDragAndDropController;

        class NativeState;
        internal::WaylandPlatformState& fPlatform;
        std::unique_ptr<NativeState> fNativeState;
        std::shared_ptr<internal::ICursorBackend> fCursor;
        WindowBackendWayland* fParentBackend = nullptr;
        std::vector<WindowBackendWayland*> fChildBackends;

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
        void updateContentScale();
        void setContentScale(double scale);
        void updateSubsurfaceInputRegion();
        void updateSubsurfacePosition();
        [[nodiscard]] bool showsClientSideDecorations() const;
        [[nodiscard]] bool showsDetachedCaption() const;
        [[nodiscard]] bool isCaptionDoubleClick(uint32_t time, Point position);
        [[nodiscard]] WindowBackendWayland* dragDropTarget();

        LWS::string_type fTitle;
        std::string fAppId;
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
        WindowShowState fDisplayState = WindowShowState::Restored;
        internal::FullScreenState fFullScreenState = internal::FullScreenState::None;
        bool fCaptionClickPending = false;
        uint32_t fLastCaptionClickTime = 0;
        Point fLastCaptionClickPosition{};
    };
}  // namespace LWS
