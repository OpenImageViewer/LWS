#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/interfaces/backends.hpp>

    #include <algorithm>
    #include <cstdint>

namespace LWS::internal
{
    constexpr int32_t waylandCaptionHeight = 32;
    constexpr int32_t waylandCloseButtonWidth = 46;
    constexpr int32_t waylandResizeBorderWidth = 8;

    enum class WaylandSurfaceRole
    {
        Content,
        Caption
    };

    enum class WaylandResizeEdge : uint32_t
    {
        None = 0,
        Top = 1,
        Bottom = 2,
        Left = 4,
        TopLeft = 5,
        BottomLeft = 6,
        Right = 8,
        TopRight = 9,
        BottomRight = 10
    };

    enum class WaylandFrameAction
    {
        Client,
        Move,
        Close,
        Resize
    };

    struct WaylandFrameConfig
    {
        WaylandSurfaceRole surface = WaylandSurfaceRole::Content;
        bool detachedCaption = false;
        bool closeButton = false;
        bool resizeEnabled = false;
    };

    struct WaylandFrameHit
    {
        WaylandFrameAction action = WaylandFrameAction::Client;
        WaylandResizeEdge edge = WaylandResizeEdge::None;

        constexpr bool operator==(const WaylandFrameHit&) const = default;
    };

    enum class WaylandDecorationMode
    {
        None,
        Pending,
        ClientSide,
        ServerSide
    };

    enum class WaylandCaptionMode
    {
        None,
        ClientSide,
        Detached
    };

    [[nodiscard]] constexpr WaylandCaptionMode waylandCaptionMode(bool childWindow, bool hostFrame,
                                                                  WaylandDecorationMode decorationMode,
                                                                  bool captionStyle)
    {
        if (childWindow || !captionStyle || decorationMode == WaylandDecorationMode::ServerSide)
            return WaylandCaptionMode::None;
        if (hostFrame)
            return WaylandCaptionMode::Detached;
        return decorationMode == WaylandDecorationMode::None || decorationMode == WaylandDecorationMode::ClientSide
                   ? WaylandCaptionMode::ClientSide
                   : WaylandCaptionMode::None;
    }

    [[nodiscard]] constexpr bool isWaylandResizeEnabled(WindowStyle styles, WindowShowState displayState,
                                                        bool fullscreen, bool childWindow)
    {
        return !fullscreen && !childWindow && displayState == WindowShowState::Restored &&
               (std::to_underlying(styles) & std::to_underlying(WindowStyle::ResizableBorder)) != 0;
    }

    [[nodiscard]] constexpr Rect waylandChildInputRect(Point position, Size childSize, Size parentSize,
                                                       bool preserveParentResizeFrame)
    {
        const int32_t width = std::max(childSize.x, 0);
        const int32_t height = std::max(childSize.y, 0);
        if (!preserveParentResizeFrame)
            return {{0, 0}, {width, height}};

        const int32_t left = std::clamp(waylandResizeBorderWidth - position.x, 0, width);
        const int32_t right = std::clamp(parentSize.x - waylandResizeBorderWidth - position.x, left, width);
        const int32_t bottom = std::clamp(parentSize.y - waylandResizeBorderWidth - position.y, 0, height);
        return {{left, 0}, {right, bottom}};
    }

    [[nodiscard]] constexpr WaylandFrameHit waylandFrameHit(Point position, Size surfaceSize,
                                                            const WaylandFrameConfig& config)
    {
        if (surfaceSize.x <= 0 || surfaceSize.y <= 0 || position.x < 0 || position.y < 0 ||
            position.x >= surfaceSize.x || position.y >= surfaceSize.y)
        {
            return {};
        }

        const bool caption = config.surface == WaylandSurfaceRole::Caption ||
                             (!config.detachedCaption && position.y < waylandCaptionHeight);
        if (caption && config.closeButton && position.x >= std::max(surfaceSize.x - waylandCloseButtonWidth, 0))
            return {.action = WaylandFrameAction::Close};

        if (config.resizeEnabled)
        {
            const bool left = position.x < waylandResizeBorderWidth;
            const bool right = position.x >= surfaceSize.x - waylandResizeBorderWidth;
            const bool top = position.y < waylandResizeBorderWidth &&
                             (config.surface == WaylandSurfaceRole::Caption || !config.detachedCaption);
            const bool bottom = config.surface == WaylandSurfaceRole::Content &&
                                position.y >= surfaceSize.y - waylandResizeBorderWidth;

            WaylandResizeEdge edge = WaylandResizeEdge::None;
            if (top && left)
                edge = WaylandResizeEdge::TopLeft;
            else if (top && right)
                edge = WaylandResizeEdge::TopRight;
            else if (bottom && left)
                edge = WaylandResizeEdge::BottomLeft;
            else if (bottom && right)
                edge = WaylandResizeEdge::BottomRight;
            else if (top)
                edge = WaylandResizeEdge::Top;
            else if (bottom)
                edge = WaylandResizeEdge::Bottom;
            else if (left)
                edge = WaylandResizeEdge::Left;
            else if (right)
                edge = WaylandResizeEdge::Right;

            if (edge != WaylandResizeEdge::None)
                return {.action = WaylandFrameAction::Resize, .edge = edge};
        }

        return {.action = caption ? WaylandFrameAction::Move : WaylandFrameAction::Client};
    }
}  // namespace LWS::internal

#endif
