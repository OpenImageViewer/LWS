#pragma once
#ifdef LWS_PLATFORM_WIN32

    #include <Windows.h>

    #include <LWS/interfaces/backends.hpp>
    #include <LWS/Win32/EventWin32.hpp>

namespace LWS::internal
{
    class DragAndDropTarget;
    class MonitorInfo;
}  // namespace LWS::internal

namespace LWS
{
    class WindowBackendWin32 : public internal::IWindowBackend
    {
      public:

        WindowBackendWin32(Window& owner, internal::MonitorInfo& monitors);
        ~WindowBackendWin32() override;

        Result create(const internal::NativeWindowConfig& config) override;
        void destroy() override;
        void show() override;
        void hide() override;
        bool getVisible() const override;
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
        Handle getHandle() const override;
        BackendId backend() const override;
        uintptr_t getCurrentMonitorHandle() const override;

        void setMenuChar(bool suppress);

      private:

        static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        LRESULT windowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        LONG composeWindowStyles() const;
        void applyWindowIcon() const;
        void updateWindowStyles();
        void updateBackgroundBrush();
        void dispatchClientAreaSizeChanged(Size framebufferSize);
        [[nodiscard]] Size getWindowSize() const;
        void setWindowed();
        void setFullScreen(bool multiMonitor);
        LRESULT getCorner(POINTS points) const;
        [[nodiscard]] bool dispatchPlatformEvent(const Win32::PlatformEvent& event, LRESULT& result);

        HWND fHwnd = nullptr;
        // Coordinate conversion runs for every input event, so refresh this cache only when the window DPI changes.
        UINT fDpi = 96;
        bool fSuppressMenuChar = false;
        Size fMinSize = {0, 0};
        Size fMaxSize = {0, 0};
        bool fEraseBackground = true;
        LLUtils::Color fBackgroundColor = {};
        HBRUSH fBackgroundBrush = nullptr;
        HICON fWindowIcon = nullptr;
        internal::FullScreenState fFullScreenState = internal::FullScreenState::Windowed;
        WINDOWPLACEMENT fSavedFullScreenPlacement = {};
        bool fVisible = false;
        bool fAlwaysOnTop = false;
        bool fTransparent = false;
        WindowStyle fWindowStyles = WindowStyle::NoStyle;
        WindowShowState fDisplayState = WindowShowState::Restored;
        internal::IWindowBackend* fParentBackend = nullptr;
        Point fLastMousePos = {};
        bool fDndEnabled = false;
        std::shared_ptr<internal::ICursorBackend> fCursor;
        std::shared_ptr<internal::DragAndDropTarget> fDragAndDrop;
        internal::MonitorInfo& fMonitors;
    };
}  // namespace LWS
#endif  // LWS_PLATFORM_WIN32
