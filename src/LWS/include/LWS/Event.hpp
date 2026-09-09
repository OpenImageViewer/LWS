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

    using EventCallback = std::move_only_function<EventResponse(const AnyEvent&)>;

    namespace internal
    {
        class ListenerState;
        class WindowBackendAccess;
    }  // namespace internal

    /// Owns one listener registration on its bound context thread.
    ///
    /// A live or disconnected bound connection must be moved, queried, disconnected, and destroyed on that thread. A
    /// default or moved-from connection is thread-neutral. Destroying a connection after its Window is safe.
    class EventConnection final
    {
      public:

        EventConnection() = default;
        ~EventConnection();

        EventConnection(const EventConnection&) = delete;
        EventConnection& operator=(const EventConnection&) = delete;
        EventConnection(EventConnection&& other) noexcept;
        EventConnection& operator=(EventConnection&& other) noexcept;

        void Disconnect();
        [[nodiscard]] bool IsConnected() const;

      private:

        friend class Window;
        friend class internal::WindowBackendAccess;

        EventConnection(std::weak_ptr<internal::ListenerState> state, uint64_t listenerId,
                        std::thread::id threadId) noexcept;
        void MoveFrom(EventConnection& other) noexcept;

        std::weak_ptr<internal::ListenerState> state_;
        uint64_t listenerId_{};
        std::thread::id threadId_{};
        bool bound_{};
    };
}  // namespace LWS
