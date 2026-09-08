#pragma once

#include <LWS/KeyCode.hpp>
#include <LWS/MouseButton.hpp>
#include <LWS/Result.hpp>
#include <LWS/WindowTypes.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <thread>
#include <variant>

namespace LWS
{
    struct EventResize
    {
        Size newClientSize;
    };
    struct EventMove
    {
        Point newPosition;
    };
    struct EventCloseRequested
    {
    };
    struct EventWindowDestroyed
    {
    };
    struct EventFocusGained
    {
    };
    struct EventFocusLost
    {
    };
    struct EventShowStateChanged
    {
        WindowShowState state;
    };
    struct EventKeyDown
    {
        KeyCode key;
        bool repeat = false;
    };
    struct EventKeyUp
    {
        KeyCode key;
    };
    struct EventMouseMove
    {
        Point position;
        Point delta;
    };
    struct EventMouseButton
    {
        MouseButton button;
        bool pressed;
        Point position;
    };
    struct EventMouseWheel
    {
        static constexpr int32_t DeltaPerStep = 120;

        int32_t delta;
        Point position;

        [[nodiscard]] constexpr double steps() const { return static_cast<double>(delta) / DeltaPerStep; }
    };
    struct EventPaint
    {
    };
    struct EventDragDropFile
    {
        std::filesystem::path fileName;
    };
    struct EventRawPlatform
    {
        uint32_t platformType;
        void* platformData = nullptr;
    };

    using AnyEvent =
        std::variant<EventResize, EventMove, EventCloseRequested, EventWindowDestroyed, EventFocusGained,
                     EventFocusLost, EventShowStateChanged, EventKeyDown, EventKeyUp, EventMouseMove, EventMouseButton,
                     EventMouseWheel, EventPaint, EventDragDropFile, EventRawPlatform>;

    enum class EventResponse
    {
        Unhandled,
        Handled
    };

    using EventCallback = std::move_only_function<bool(const AnyEvent&)>;

    namespace internal
    {
        class ListenerState;
        class WindowBackendAccess;
    }  // namespace internal

    using EventListenerToken = uint64_t;
    class Window;
    struct EventListenerGuard
    {
        Window* window = nullptr;
        EventListenerToken token = 0;
        EventListenerGuard() = default;
        EventListenerGuard(Window* w, EventListenerToken t) : window(w), token(t) {}
        EventListenerGuard(const EventListenerGuard&) = delete;
        EventListenerGuard& operator=(const EventListenerGuard&) = delete;
        EventListenerGuard(EventListenerGuard&& other) noexcept : window(other.window), token(other.token)
        {
            other.window = nullptr;
        }
        EventListenerGuard& operator=(EventListenerGuard&& other) noexcept;
        ~EventListenerGuard();
    };
}  // namespace LWS
