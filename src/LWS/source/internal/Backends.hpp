#pragma once

// Private implementation contracts. Public consumers must not include this file.

#include <LWS/Bitmap.hpp>
#include <LWS/CursorShape.hpp>
#include <LWS/Event.hpp>
#include <LWS/Result.hpp>
#include <LWS/WindowTypes.hpp>

#include <functional>
#include <memory>

namespace LWS
{
    class PlatformContext;

    namespace internal
    {
        enum class LockMouseToWindowMode
        {
            NoLock,
            LockResize,
            LockMove
        };

        enum class FullScreenState
        {
            None,
            Windowed,
            SingleScreen,
            MultiScreen
        };

        struct NativeWindowPlacement
        {
            Point position;
            Size size;
            WindowShowState displayState{WindowShowState::Restored};
        };

        struct NativeWindowConfig
        {
            Point position{100, 100};
            Size size{800, 600};
            string_type title;
            WindowStyle styles{WindowStyle::NoStyle};
            WindowShowState displayState{WindowShowState::Restored};
            bool visible{};
            bool eraseBackground{true};
            bool alwaysOnTop{};
            bool transparent{};
            Size minSize{};
            Size maxSize{};
        };

        class ICursorBackend
        {
          public:

            virtual ~ICursorBackend() = default;
            virtual void setVisible(bool visible) = 0;
            virtual void setCursorShape(CursorShape shape) = 0;
            [[nodiscard]] virtual Result setCustomCursor(const BitmapBuffer& bitmap, Point hotspot) = 0;
            [[nodiscard]] virtual BackendId backend() const = 0;
        };

        class IWindowBackend
        {
          public:

            explicit IWindowBackend(Window& owner) : owner_(owner) {}
            virtual ~IWindowBackend() = default;

            [[nodiscard]] virtual Result create(const NativeWindowConfig& config) = 0;
            virtual void destroy() = 0;
            [[nodiscard]] virtual bool isConfigured() const { return true; }
            virtual void show() = 0;
            virtual void hide() = 0;
            [[nodiscard]] virtual bool getVisible() const = 0;
            virtual void setDisplayState(WindowShowState state) = 0;
            virtual void maximize() = 0;
            [[nodiscard]] virtual WindowShowState getDisplayState() const = 0;
            virtual void setTitle(const string_type& title) = 0;
            [[nodiscard]] virtual string_type getTitle() const = 0;
            [[nodiscard]] virtual Result setWindowIcon(const BitmapBuffer* icon) = 0;
            virtual void setPosition(Point position) = 0;
            [[nodiscard]] virtual Point getPosition() const = 0;
            [[nodiscard]] virtual uintptr_t getCurrentMonitorHandle() const { return 0; }
            virtual void setSize(Size size) = 0;
            [[nodiscard]] virtual Size getClientSize() const = 0;
            [[nodiscard]] virtual Size getFramebufferSize() const { return getClientSize(); }
            virtual void setPlacement(const NativeWindowPlacement& placement) = 0;
            virtual void setMinMaxSize(Size minSize, Size maxSize) = 0;
            [[nodiscard]] virtual Size getMinSize() const = 0;
            [[nodiscard]] virtual Size getMaxSize() const = 0;
            virtual void setWindowStyles(WindowStyle styles, bool enable) = 0;
            [[nodiscard]] virtual WindowStyle getWindowStyles() const = 0;
            virtual void setForeground() = 0;
            [[nodiscard]] virtual bool isInFocus() const = 0;
            virtual void setAlwaysOnTop(bool onTop) = 0;
            [[nodiscard]] virtual bool getAlwaysOnTop() const = 0;
            virtual void setTransparent(bool transparent) = 0;
            [[nodiscard]] virtual bool getTransparent() const = 0;
            virtual void setBackgroundColor(LLUtils::Color color) = 0;
            virtual void setEraseBackground(bool erase) = 0;
            [[nodiscard]] virtual bool getEraseBackground() const = 0;
            virtual void setFullScreenState(FullScreenState state) = 0;
            [[nodiscard]] virtual FullScreenState getFullScreenState() const = 0;
            [[nodiscard]] virtual bool isMouseInClientRect() const = 0;
            [[nodiscard]] virtual Point getMousePosition() const = 0;
            virtual void setLockMouseToWindowMode(LockMouseToWindowMode mode) = 0;
            [[nodiscard]] virtual Result setPointerLocked(bool locked) = 0;
            virtual void setCursor(std::shared_ptr<ICursorBackend> cursor) = 0;
            virtual void setParent(IWindowBackend* parent) = 0;
            [[nodiscard]] virtual Result enableDragAndDrop(bool enable) = 0;
            [[nodiscard]] virtual Result presentBitmap(const BitmapBuffer&) { return Result::NotSupported; }
            [[nodiscard]] virtual Handle getHandle() const = 0;
            [[nodiscard]] virtual BackendId backend() const = 0;

          protected:

            EventResponse dispatchEvent(const AnyEvent& event);
            [[nodiscard]] Window& owner() const { return owner_; }

          private:

            Window& owner_;
        };

        class ITimerBackend
        {
          public:

            using Callback = std::function<void()>;
            virtual ~ITimerBackend() = default;
            virtual void setTargetWindow(Handle windowHandle) = 0;
            [[nodiscard]] virtual uint32_t getInterval() const = 0;
            virtual void setInterval(uint32_t interval) = 0;
            virtual void setCallback(Callback callback) = 0;
        };

        class IHighPrecisionTimerBackend
        {
          public:

            virtual ~IHighPrecisionTimerBackend() = default;
            virtual void setRepeatInterval(uint32_t repeatInterval) = 0;
            virtual void setDueTime(uint32_t dueTime) = 0;
            [[nodiscard]] virtual bool getEnabled() const = 0;
            virtual void enable(bool enabled) = 0;
        };

        [[nodiscard]] std::unique_ptr<ICursorBackend> createDefaultCursorBackend();
        [[nodiscard]] std::unique_ptr<ITimerBackend> createTimerBackend(PlatformContext& platform);
        [[nodiscard]] std::unique_ptr<IHighPrecisionTimerBackend> createHighPrecisionTimerBackend(
            PlatformContext& platform, ITimerBackend::Callback callback);
    }  // namespace internal
}  // namespace LWS
