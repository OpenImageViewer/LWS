#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <variant>
#include <vector>

#include <LWS/KeyCode.hpp>
#include <LWS/MouseButton.hpp>
#include <LWS/WindowDisplayState.hpp>
#include <LLUtils/Point.h>

namespace LWS
{
    using Size = LLUtils::PointI32;
    using Point = LLUtils::PointI32;

    enum class BackendId;

    struct EventResize { Size newClientSize; };
    struct EventMove { Point newPosition; };
    struct EventClose {};
    struct EventWindowDestroyed {};
    struct EventFocusGained {};
    struct EventFocusLost {};
    struct EventDisplayStateChanged { WindowDisplayState state; };
    struct EventKeyDown { KeyCode key; bool repeat = false; };
    struct EventKeyUp { KeyCode key; };
    struct EventMouseMove { Point position; Point delta; };
    struct EventMouseButton { MouseButton button; bool pressed; Point position; };
    struct EventMouseWheel { int32_t delta; Point position; };
    struct EventPaint {};
    struct EventDragDropFile { std::filesystem::path fileName; };
    struct EventRawPlatform { uint32_t platformType; void* platformData = nullptr; };

    using AnyEvent = std::variant<
        EventResize, EventMove, EventClose, EventWindowDestroyed,
        EventFocusGained, EventFocusLost, EventDisplayStateChanged,
        EventKeyDown, EventKeyUp, EventMouseMove, EventMouseButton,
        EventMouseWheel, EventPaint, EventDragDropFile, EventRawPlatform>;

    using EventCallback = std::move_only_function<bool(const AnyEvent&)>;
    using EventListenerToken = uint64_t;

    class Window;
    struct EventListenerGuard
    {
        Window* window = nullptr;
        EventListenerToken token = 0;

        EventListenerGuard() = default;
        [[nodiscard]] EventListenerGuard(Window* w, EventListenerToken t) : window(w), token(t) {}
        EventListenerGuard(const EventListenerGuard&) = delete;
        EventListenerGuard& operator=(const EventListenerGuard&) = delete;
        EventListenerGuard(EventListenerGuard&& other) noexcept : window(other.window), token(other.token)
        {
            other.window = nullptr;
        }
        EventListenerGuard& operator=(EventListenerGuard&& other) noexcept;
        ~EventListenerGuard();
    };
}
