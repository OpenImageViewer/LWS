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

        [[nodiscard]] Result SetTitle(const string_type& title);
        [[nodiscard]] string_type GetTitle() const;
        [[nodiscard]] Result SetVisible(bool visible);
        [[nodiscard]] bool IsVisible() const;

        /// Returns the last published client-area size in both logical units and pixels.
        /// The client area excludes title bars, borders, and other window decorations.
        ///
        /// `logical` is the stable layout size the application aims to preserve when
        /// display scaling changes. It describes the space used for UI layout and
        /// requested window sizes, independently of how many pixels draw that space.
        /// On Win32, these are 96-DPI units; on Wayland, they are surface coordinates.
        ///
        /// For example, preserving a width of 600 logical units means:
        /// - At 100% scaling: 600 pixels.
        /// - At 150% scaling: 900 pixels.
        /// - At 200% scaling: 1200 pixels.
        /// The logical width stays 600 while the pixel width changes.
        /// Resizing, native window constraints, or rounding can still change the
        /// reported logical size; it is not a fixed measurement in inches or millimetres.
        ///
        /// `pixels`, when present, is the exact client-area size needed for rendering.
        /// On Win32, it comes from native client dimensions. On Wayland, each logical
        /// dimension is multiplied by the surface scale and rounded upward.
        ///
        /// When pixels are available, both sizes describe the same backend update.
        /// Size listeners observe the published pair before running; show-state listeners
        /// can run before that publication and still observe the previous pair.
        /// Nested dispatch can publish a newer pair before an outer listener resumes.
        /// Scale() returns pixels per logical unit on each axis, including rounding;
        /// it returns nullopt if pixels are absent or either logical dimension is zero.
        /// Use `logical` for UI layout and `pixels` for rendering dimensions.
        ///
        /// When native metrics are unavailable, logical size comes from the backend
        /// while created or the stored configuration otherwise, and pixels is nullopt.
        /// This includes initial Wayland configuration, remapping, and native teardown
        /// or backend failure. Pixels can remain available during orderly cleanup.
        [[nodiscard]] ClientAreaMetrics GetClientAreaMetrics() const;

        /// Requests movement, resizing, or both; omitted fields remain unchanged.
        /// Does not issue a show-state request or override one made by a geometry listener.
        /// Requires a created window and at least one field. Supplied size must be positive.
        /// Explicit position on a Wayland top-level window returns NotSupported without resizing.
        /// Win32 submits combined geometry in one native operation. Wayland child position
        /// and content may take effect on separate commits; completion is not synchronous.
        [[nodiscard]] Result RequestPlacement(const WindowPlacementRequest& request);
        /// Returns logical client size and optional position, using stored values outside creation.
        /// Win32 top-level position is the restored placement in DPI-normalized screen coordinates;
        /// child position is relative to the parent's client area. Wayland top-level position is unavailable.
        /// Position and size are not an atomic native observation.
        [[nodiscard]] WindowPlacement GetPlacement() const;
        /// Sets application resize limits for a created window. Dimensions must be nonnegative;
        /// each nonzero maximum must be at least its minimum. Zero means no application limit on that axis.
        [[nodiscard]] Result SetClientSizeLimits(ClientSizeLimits limits);
        /// Returns backend limits while created, otherwise stored configuration limits, in logical units.
        [[nodiscard]] ClientSizeLimits GetClientSizeLimits() const;
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
        [[nodiscard]] bool IsAlwaysOnTop() const;
        [[nodiscard]] Result SetTransparent(bool transparent);
        [[nodiscard]] bool IsTransparent() const;
        [[nodiscard]] Result SetBackgroundColor(LLUtils::Color color);
        [[nodiscard]] Result SetEraseBackground(bool erase);
        [[nodiscard]] bool IsBackgroundErasureEnabled() const;
        [[nodiscard]] Result SetDragAndDropEnabled(bool enable);

        [[nodiscard]] bool IsMouseInClientRect() const;
        [[nodiscard]] Point GetMousePosition() const;
        [[nodiscard]] Result SetPointerLocked(bool locked);
        [[nodiscard]] Result BeginWindowDrag(WindowDragOperation operation);

        /// Applies a cursor while preserving visibility; select CursorShape::Arrow to reset.
        /// May be configured before Create(); a moved-from cursor is invalid.
        [[nodiscard]] Result SetMouseCursor(Cursor cursor);
        [[nodiscard]] Result SetMouseCursorVisible(bool visible);
        /// Applies a custom icon, or clears it for nullopt, retaining native resource reuse.
        /// May be configured before Create(); an engaged moved-from icon is invalid.
        [[nodiscard]] Result SetWindowIcon(std::optional<WindowIcon> icon);

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
