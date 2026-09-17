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
        Window* parent = nullptr;
        string_type title;
        std::optional<Point> position;
        /// Initial drawable client-area size in logical units, excluding native outer decorations.
        LogicalSize clientSize{800, 600};
        WindowStyleFlags styles;
        WindowShowState showState{WindowShowState::Restored};
        LLUtils::Color backgroundColor{};
        bool visible{false};
        bool eraseBackground{true};
        bool alwaysOnTop{false};
        bool transparent{false};
        bool dragAndDropEnabled{false};
        ClientSizeLimits clientSizeLimits;
    };
}  // namespace LWS
