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

    struct LogicalSize
    {
        int32_t x{};
        int32_t y{};

        explicit operator Size() const { return {x, y}; }
        bool operator==(const LogicalSize&) const = default;
    };

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

    struct ContentScale
    {
        double x{1.0};
        double y{1.0};

        auto operator<=>(const ContentScale&) const = default;
    };

    struct ClientAreaSize
    {
        /// Observed drawable client size in 96-DPI logical units or Wayland surface coordinates.
        LogicalSize logical;
        /// Exact pixel dimensions required by the native renderer or presentation buffer.
        PixelSize pixels;

        [[nodiscard]] ContentScale Scale() const
        {
            return {
                static_cast<double>(pixels.x) / logical.x,
                static_cast<double>(pixels.y) / logical.y,
            };
        }

        bool operator==(const ClientAreaSize&) const = default;
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
        LogicalSize minClientSize{};
        LogicalSize maxClientSize{};
    };
}  // namespace LWS
