#pragma once

#include <LWS/StringDefs.hpp>
#include <LWS/WindowShowState.hpp>

#include <LLUtils/BitFlags.h>
#include <LLUtils/Color.h>
#include <LLUtils/EnumClassBitwise.h>
#include <LLUtils/Point.h>
#include <LLUtils/Rect.h>

#include <cstdint>
#include <optional>

namespace LWS
{
    using Size = LLUtils::PointI32;
    using Point = LLUtils::PointI32;
    using Rect = LLUtils::RectI32;
    using Handle = uintptr_t;

    class Window;

    /// Layout dimensions in 96-DPI units on Win32 or surface coordinates on Wayland.
    /// Applications aim to preserve this size across display-scale changes while pixel dimensions change.
    /// Resizing, native constraints, and integer rounding can still change the reported logical size.
    struct LogicalSize
    {
        int32_t x{};
        int32_t y{};

        explicit operator Size() const { return {x, y}; }
        bool operator==(const LogicalSize&) const = default;
    };

    /// Raster dimensions in pixels, used to size render targets and presentation buffers.
    struct PixelSize
    {
        int32_t x{};
        int32_t y{};

        explicit operator Size() const { return {x, y}; }
        bool operator==(const PixelSize&) const = default;
    };

    enum class BackendId
    {
        Undefined,
        Win32,
        WinUI,
        Wayland,
        X11
    };

    enum class WindowStyle : uint32_t
    {
        NoStyle = 0,
        Caption = 1U << 0,
        CloseButton = 1U << 1,
        ResizableBorder = 1U << 2,
        MinimizeButton = 1U << 3,
        MaximizeButton = 1U << 4
    };

    LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(WindowStyle)
    using WindowStyleFlags = LLUtils::BitFlags<WindowStyle>;

    enum class WindowMode
    {
        Windowed,
        Fullscreen,
        FullscreenAllMonitors
    };

    enum class CenterTarget
    {
        Parent,
        CurrentMonitor,
        PrimaryMonitor
    };

    enum class WindowDragOperation
    {
        Move,
        ResizeNearest
    };

    /// Direction a popup extends from its anchor point.
    /// Each value names the extension direction and thereby the popup corner placed at the anchor: DownRight
    /// extends toward the bottom right and pins the top-left corner, while UpLeft pins the bottom-right corner.
    enum class PopupGravity
    {
        DownRight,
        DownLeft,
        UpRight,
        UpLeft
    };

    /// Parent-relative placement for popup windows, in logical units of the parent's client area.
    /// offset moves the anchor before gravity is applied. adjustToScreen lets the backend flip or
    /// slide the popup to stay inside the monitor work area; the resulting position is resolved by
    /// the backend and is not required to match the requested anchor.
    struct PopupPlacement
    {
        Point anchor;
        PopupGravity gravity{PopupGravity::DownRight};
        Point offset{};
        /// Requested popup client size; both dimensions must be positive.
        LogicalSize size;
        bool adjustToScreen{true};

        bool operator==(const PopupPlacement&) const = default;
    };

    struct WindowPlacement
    {
        std::optional<Point> position;
        /// Drawable client-area size in logical units, excluding native outer decorations.
        LogicalSize clientSize;
    };

    /// Omitted fields remain unchanged. Position follows WindowPlacement coordinates;
    /// clientSize excludes decorations and uses logical layout units.
    struct WindowPlacementRequest
    {
        std::optional<Point> position;
        std::optional<LogicalSize> clientSize;
    };

    /// Application-specified client-area resize limits in logical units.
    /// Zero leaves that axis unconstrained by the application; native limits may still apply.
    struct ClientSizeLimits
    {
        LogicalSize minimum{};
        LogicalSize maximum{};

        bool operator==(const ClientSizeLimits&) const = default;
    };

    struct ContentScale
    {
        double x{1.0};
        double y{1.0};

        auto operator<=>(const ContentScale&) const = default;
    };

    /// Client-area dimensions excluding title bars, borders, and other decorations.
    /// When pixels are available, both dimensions describe the same backend update.
    /// Window::GetClientAreaMetrics() documents their source and availability.
    struct ClientAreaMetrics
    {
        /// Client-area dimensions in logical layout units.
        LogicalSize logical;
        /// Exact rendering dimensions, absent when native metrics are unavailable.
        /// An available zero-sized client area is distinct from unavailable metrics.
        std::optional<PixelSize> pixels;

        /// Effective pixels per logical unit on each axis, derived from this pair.
        /// Integer rounding can make the axes differ from each other and from nominal display scale.
        /// Returns nullopt when pixels are absent or either logical dimension is zero.
        [[nodiscard]] std::optional<ContentScale> Scale() const
        {
            std::optional<ContentScale> scale;
            if (pixels.has_value() && logical.x != 0 && logical.y != 0)
            {
                scale = ContentScale{
                    static_cast<double>(pixels->x) / logical.x,
                    static_cast<double>(pixels->y) / logical.y,
                };
            }
            return scale;
        }

        bool operator==(const ClientAreaMetrics&) const = default;
    };

    struct WindowConfig
    {
        /// Popup windows require a created parent; child windows use the parent's coordinate space for
        /// placement, anchoring, and native lifetime ordering.
        Window* parent = nullptr;
        string_type title;
        std::optional<Point> position;
        /// Initial drawable client-area size in logical units, excluding native outer decorations.
        LogicalSize clientSize{800, 600};
        WindowStyleFlags styles{};
        WindowShowState showState{WindowShowState::Restored};
        LLUtils::Color backgroundColor{};
        bool visible{false};
        bool eraseBackground{true};
        bool alwaysOnTop{false};
        bool transparent{false};
        bool dragAndDropEnabled{false};
        ClientSizeLimits clientSizeLimits;
        /// When set, creates a borderless popup window that never activates and is placed relative
        /// to the parent's client area. A popup requires parent and rejects explicit position, window
        /// styles, non-restored show state, always-on-top, transparency, drag and drop, and size
        /// limits; it uses its requested size, and the backend may adjust the position to fit the
        /// monitor work area. On Wayland the compositor owns dismissal and EventPopupDismissed fires
        /// when it dismisses the popup.
        std::optional<PopupPlacement> popupPlacement;
    };
}  // namespace LWS
