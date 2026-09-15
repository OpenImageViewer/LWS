#pragma once

#include <LWS/Bitmap.hpp>
#include <LWS/Cursor.hpp>
#include <LWS/Event.hpp>
#include <LWS/Platform.hpp>
#include <LWS/Result.hpp>
#include <LWS/WindowIcon.hpp>
#include <LWS/WindowTypes.hpp>

#include <expected>
#include <memory>
#include <optional>

namespace LWS::internal
{
    class WindowBackendAccess;
}

namespace LWS
{
    /// A stable-address window permanently bound to one active PlatformContext.
    ///
    /// Failed Create() attempts may retry. After the first successful native lifetime ends, the object is terminal and
    /// another native lifetime requires another Window. The borrowed context must outlive this complete C++ object.
    ///
    /// @par Thread safety
    /// Every operation requires the bound context thread. Debug builds assert affinity; wrong-thread release use is
    /// undefined. The C++ object must survive all active native/event dispatch, including nested dispatch.
    /// Destroy() may run from a callback; deleting the executing C++ object may not.
    /// Typed native handles are borrowed from Create() until native teardown begins, including orderly cleanup
    /// notification. Backend failure invalidates access immediately. Clients must not destroy the native window directly.
    class Window final
    {
      public:

        explicit Window(PlatformContext& platform);
        ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        [[nodiscard]] Result Create(const WindowConfig& config = {});
        [[nodiscard]] Result Destroy();
        [[nodiscard]] bool IsCreated() const;

        [[nodiscard]] PlatformContext& GetPlatformContext();
        [[nodiscard]] const PlatformContext& GetPlatformContext() const;
        [[nodiscard]] BackendId GetBackendId() const;

        [[nodiscard]] Result SetTitle(const string_type& title);
        [[nodiscard]] string_type GetTitle() const;
        [[nodiscard]] Result SetVisible(bool visible);
        [[nodiscard]] bool GetVisible() const;

        [[nodiscard]] Result SetPosition(Point position);
        [[nodiscard]] std::optional<Point> GetPosition() const;
        /// Requests a drawable client area in logical units; native outer decorations are excluded.
        [[nodiscard]] Result RequestClientSize(LogicalSize size);
        [[nodiscard]] LogicalSize GetClientSize() const;
        [[nodiscard]] std::expected<ClientAreaSize, Result> GetClientAreaSize() const;
        [[nodiscard]] Result SetPlacement(const WindowPlacement& placement);
        [[nodiscard]] WindowPlacement GetPlacement() const;
        [[nodiscard]] Result SetMinMaxClientSize(LogicalSize minimum, LogicalSize maximum);
        [[nodiscard]] LogicalSize GetMinClientSize() const;
        [[nodiscard]] LogicalSize GetMaxClientSize() const;
        [[nodiscard]] Result Center(CenterTarget target);

        [[nodiscard]] Result SetWindowMode(WindowMode mode);
        [[nodiscard]] WindowMode GetWindowMode() const;
        [[nodiscard]] Result RequestShowState(WindowShowState state);
        /// Requests windowed maximization of a top-level window, leaving fullscreen if necessary.
        /// Win32 keeps the current monitor and retains the normal client size where it fits.
        /// On Wayland the compositor chooses and asynchronously confirms the state, output, and geometry.
        [[nodiscard]] Result RequestMaximize();
        [[nodiscard]] WindowShowState GetShowState() const;
        [[nodiscard]] bool IsConfigured() const;

        [[nodiscard]] Result RequestActivation();
        [[nodiscard]] bool HasKeyboardFocus() const;
        [[nodiscard]] Result SetWindowStyles(WindowStyleFlags styles);
        [[nodiscard]] WindowStyleFlags GetWindowStyles() const;
        [[nodiscard]] Result SetAlwaysOnTop(bool onTop);
        [[nodiscard]] bool GetAlwaysOnTop() const;
        [[nodiscard]] Result SetTransparent(bool transparent);
        [[nodiscard]] bool GetTransparent() const;
        [[nodiscard]] Result SetBackgroundColor(LLUtils::Color color);
        [[nodiscard]] Result SetEraseBackground(bool erase);
        [[nodiscard]] bool GetEraseBackground() const;
        [[nodiscard]] Result EnableDragAndDrop(bool enable);

        [[nodiscard]] bool IsMouseInClientRect() const;
        [[nodiscard]] Point GetMousePosition() const;
        [[nodiscard]] Result SetPointerLocked(bool locked);
        [[nodiscard]] Result BeginWindowDrag(WindowDragOperation operation);

        [[nodiscard]] Result SetMouseCursor(Cursor cursor);
        [[nodiscard]] Result ResetMouseCursor();
        [[nodiscard]] Result SetMouseCursorVisible(bool visible);
        [[nodiscard]] Result SetWindowIcon(WindowIcon icon);
        [[nodiscard]] Result ResetWindowIcon();

        [[nodiscard]] Window* GetParent() const;
        /// Registrations made during a listener traversal become active when its outermost traversal completes.
        /// Publication precedes retired-capture destruction, which may itself reenter dispatch.
        /// Destruction notifications include pending registrations so newly attached resources can still clean up.
        [[nodiscard]] std::expected<EventConnection, Result> Listen(EventCallback callback);
        /// Borrows pixels only for this call; the caller may reuse or release them when it returns.
        /// Wayland copies into reusable shared memory and coalesces pending frames while the compositor is busy.
        [[nodiscard]] Result PresentBitmap(const BitmapBuffer& bitmap);

      private:

        friend class Timer;
        [[nodiscard]] std::expected<EventConnection, Result> Listen(EventCallback callback, bool beforeUserCallbacks);

        friend class internal::WindowBackendAccess;

        [[nodiscard]] EventResponse DispatchEvent(const AnyEvent& event);

        [[nodiscard]] bool CanDestroy() const;
#ifndef NDEBUG
        size_t dispatchDepth_{};
#endif
        PlatformContext& platform_;
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
