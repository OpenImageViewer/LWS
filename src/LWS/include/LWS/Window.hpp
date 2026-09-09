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
    /// undefined. Typed native handles are borrowed and stable only from successful Create() until Destroy() begins.
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
        /// Requests a drawable client area in native client units; native outer decorations are excluded.
        [[nodiscard]] Result RequestClientSize(Size size);
        [[nodiscard]] Size GetClientSize() const;
        [[nodiscard]] Result SetPlacement(const WindowPlacement& placement);
        [[nodiscard]] WindowPlacement GetPlacement() const;
        [[nodiscard]] Result SetMinMaxClientSize(Size minimum, Size maximum);
        [[nodiscard]] Size GetMinClientSize() const;
        [[nodiscard]] Size GetMaxClientSize() const;
        [[nodiscard]] Result Center(CenterTarget target);

        [[nodiscard]] Result SetWindowMode(WindowMode mode);
        [[nodiscard]] WindowMode GetWindowMode() const;
        [[nodiscard]] Result RequestShowState(WindowShowState state);
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
        [[nodiscard]] std::expected<EventConnection, Result> Listen(EventCallback callback);
        [[nodiscard]] Result PresentBitmap(const BitmapBuffer& bitmap);

      private:

        friend class Timer;
        [[nodiscard]] std::expected<EventConnection, Result> Listen(EventCallback callback, bool beforeUserCallbacks);

        friend class internal::WindowBackendAccess;

        [[nodiscard]] EventResponse DispatchEvent(const AnyEvent& event);

        PlatformContext& platform_;
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
