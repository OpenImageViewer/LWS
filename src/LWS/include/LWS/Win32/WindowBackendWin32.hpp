#pragma once
#ifdef LWS_PLATFORM_WIN32

#include <Windows.h>

#include <map>

#include <LWS/interfaces/backends.hpp>

namespace LWS::internal
{
    class DragAndDropTarget;
}

namespace LWS
{
    class WindowBackendWin32 : public IWindowBackend
    {
    public:
        WindowBackendWin32();
        ~WindowBackendWin32() override;

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
        Result enableDragAndDrop(bool enable) override;
        EventListenerToken addListener(EventCallback cb) override;
        void removeListener(EventListenerToken token) override;
        void injectRawEvent(void* platformEvent) override;
        Handle getHandle() const override;
        BackendId backend() const override;

        void setMenuChar(bool suppress);
        bool getMenuChar() const;

    private:
        static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

        LRESULT windowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
        LONG composeWindowStyles() const;
        void updateWindowStyles();
        void updateBackgroundBrush();
        void setWindowed();
        void setFullScreen(bool multiMonitor);
        LRESULT getCorner(POINTS points) const;
        void dispatchEvent(const AnyEvent& event_data);

        HWND fHwnd = nullptr;
        bool fSuppressMenuChar = false;
        Size fMinSize = { 0, 0 };
        Size fMaxSize = { 0, 0 };
        bool fEraseBackground = true;
        LLUtils::Color fBackgroundColor = {};
        HBRUSH fBackgroundBrush = nullptr;
        LockMouseToWindowMode fLockMode = LockMouseToWindowMode::NoLock;
        DoubleClickMode fDoubleClickMode = DoubleClickMode::NotSet;
        FullScreenState fFullScreenState = FullScreenState::Windowed;
        WINDOWPLACEMENT fSavedFullScreenPlacement = {};
        bool fVisible = false;
        bool fAlwaysOnTop = false;
        bool fTransparent = false;
        WindowStyle fWindowStyles = WindowStyle::NoStyle;
        WindowDisplayState fDisplayState = WindowDisplayState::Restored;
        IWindowBackend* fParentBackend = nullptr;
        Point fLastMousePos = {};
        bool fDndEnabled = false;
        std::shared_ptr<ICursorBackend> fCursor;
        std::shared_ptr<internal::DragAndDropTarget> fDragAndDrop;
        uint64_t fNextToken = 1;
        std::map<EventListenerToken, EventCallback> fListeners;
    };
}
#endif  // LWS_PLATFORM_WIN32
