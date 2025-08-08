#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

#include <LLUtils/Color.h>
#include <LLUtils/Point.h>
#include <LLUtils/Rect.h>
#include <LWS/CursorShape.hpp>
#include <LWS/Event.hpp>
#include <LWS/Result.hpp>
#include <LWS/StringDefs.hpp>
#include <LWS/WindowDisplayState.hpp>

namespace LWS
{
    using Size = LLUtils::PointI32;
    using Point = LLUtils::PointI32;
    using Rect = LLUtils::RectI32;
    using Handle = uintptr_t;

    enum class BackendId { Undefined, Win32, WinUI, Wayland, X11 };

    enum class WindowStyle : uint32_t
    {
        NoStyle = 0,
        Caption = 1 << 0,
        CloseButton = 1 << 1,
        ResizableBorder = 1 << 2,
        MinimizeButton = 1 << 3,
        MaximizeButton = 1 << 4,
        ChildWindow = 1 << 5,
    };

    enum class DoubleClickMode { NotSet, Default };
    enum class LockMouseToWindowMode { NoLock, LockResize, LockMove };
    enum class FullScreenState { None, Windowed, SingleScreen, MultiScreen };

    struct WindowPlacement
    {
        Point position;
        Size size;
        WindowDisplayState displayState = WindowDisplayState::Restored;
    };

    struct WindowConfig
    {
        Point position = { 100, 100 };
        Size size = { 800, 600 };
        LWS::string_type title;
        WindowStyle styles = WindowStyle::Caption;
        WindowDisplayState displayState = WindowDisplayState::Restored;
        bool visible = false;
        bool eraseBackground = true;
        bool alwaysOnTop = false;
        bool transparent = false;
        Size minSize = { 0, 0 };
        Size maxSize = { 0, 0 };
    };

    struct BitmapBuffer
    {
        std::span<const std::byte> pixels;
        uint8_t bitsPerPixel = 32;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t rowPitch = 0;
    };

    class ICursorBackend
    {
    public:
        virtual ~ICursorBackend() = default;
        virtual void setVisible(bool visible) = 0;
        virtual void setCursorShape(CursorShape shape) = 0;
        virtual void setCustomCursor(const BitmapBuffer& bmp) = 0;
        virtual BackendId backend() const = 0;
    };

    class IWindowBackend
    {
    public:
        virtual ~IWindowBackend() = default;

        [[nodiscard]] virtual Result create(const WindowConfig& config) = 0;
        virtual void destroy() = 0;
        virtual void show() = 0;
        virtual void hide() = 0;
        virtual bool getVisible() const = 0;
        virtual void setDisplayState(WindowDisplayState state) = 0;
        virtual WindowDisplayState getDisplayState() const = 0;
        virtual void setTitle(const LWS::string_type& title) = 0;
        virtual LWS::string_type getTitle() const = 0;
        virtual void setWindowIcon(const std::filesystem::path& iconPath) = 0;
        virtual void setPosition(Point pos) = 0;
        virtual Point getPosition() const = 0;
        virtual void setSize(Size sz) = 0;
        virtual Size getClientSize() const = 0;
        virtual Rect getClientRect() const = 0;
        virtual Size getWindowSize() const = 0;
        virtual void setPlacement(const WindowPlacement& placement) = 0;
        virtual WindowPlacement getPlacement() const = 0;
        virtual void setMinMaxSize(Size minSize, Size maxSize) = 0;
        virtual Size getMinSize() const = 0;
        virtual Size getMaxSize() const = 0;
        virtual void setWindowStyles(WindowStyle styles, bool enable) = 0;
        virtual WindowStyle getWindowStyles() const = 0;
        virtual void setForeground() = 0;
        virtual void setFocus() = 0;
        virtual bool isInFocus() const = 0;
        virtual void setAlwaysOnTop(bool onTop) = 0;
        virtual bool getAlwaysOnTop() const = 0;
        virtual void setTransparent(bool transparent) = 0;
        virtual bool getTransparent() const = 0;
        virtual void setBackgroundColor(LLUtils::Color color) = 0;
        virtual LLUtils::Color getBackgroundColor() const = 0;
        virtual void setEraseBackground(bool erase) = 0;
        virtual bool getEraseBackground() const = 0;
        virtual void setFullScreenState(FullScreenState state) = 0;
        virtual FullScreenState getFullScreenState() const = 0;
        virtual bool isFullScreen() const = 0;
        virtual void toggleFullScreen(bool multiMonitor = false) = 0;
        virtual bool isMouseInClientRect() const = 0;
        virtual bool isUnderMouseCursor() const = 0;
        virtual Point getMousePosition() const = 0;
        virtual void setLockMouseToWindowMode(LockMouseToWindowMode mode) = 0;
        virtual LockMouseToWindowMode getLockMouseToWindowMode() const = 0;
        virtual void setDoubleClickMode(DoubleClickMode mode) = 0;
        virtual DoubleClickMode getDoubleClickMode() const = 0;
        virtual void setCursor(std::shared_ptr<ICursorBackend> cursor) = 0;
        virtual void setParent(IWindowBackend* parent) = 0;
        virtual void enableDragAndDrop(bool enable) = 0;
        [[nodiscard]] virtual EventListenerToken addListener(EventCallback cb) = 0;
        virtual void removeListener(EventListenerToken token) = 0;
        virtual void injectRawEvent(void* platformEvent) = 0;
        [[nodiscard]] virtual Handle getHandle() const = 0;
        [[nodiscard]] virtual BackendId backend() const = 0;
    };

    namespace internal
    {
        [[nodiscard]] std::unique_ptr<IWindowBackend> createDefaultWindowBackend();
        [[nodiscard]] std::unique_ptr<ICursorBackend> createDefaultCursorBackend();
    }
}