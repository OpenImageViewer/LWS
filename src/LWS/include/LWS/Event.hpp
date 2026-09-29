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
#include <string>

namespace LWS
{
    /// Publishes a paired logical/pixel update, including pixel-only changes caused by display scaling.
    /// Pixels are present; unconfigured size/scale updates wait for native configuration.
    /// GetClientAreaMetrics() reflects this update before listeners run; nested dispatch may replace it.
    struct EventClientAreaSizeChanged
    {
        ClientAreaMetrics size;
    };
    struct EventMove
    {
        Point newPosition;
    };
    struct EventCloseRequested
    {
    };
    /// Non-cancellable cleanup notification. False means backend loss has already invalidated native handles.
    /// The flag describes entry to this callback sequence; recheck the context after nested pumping.
    struct EventWindowDestroying
    {
        bool nativeResourcesAvailable{true};
    };
    struct EventWindowDestroyed
    {
    };
    /// The platform dismissed a popup on its own (Wayland popup_done: click outside its grab, parent
    /// unmapped, or another popup took the grab). Native resources are already released; listeners
    /// run on a window that is terminal afterwards, and EventWindowDestroyed follows. Windows never
    /// emits this event because popup dismissal there is application-owned.
    struct EventPopupDismissed
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
    struct EventTextInput
    {
        std::string text;
    };  // Committed UTF-8, separate from physical keys.
    struct EventMouseLeave
    {
    };
    struct EventMouseCaptureLost
    {
    };

    using AnyEvent = std::variant<EventClientAreaSizeChanged, EventMove, EventCloseRequested, EventWindowDestroyed,
                                  EventFocusGained, EventFocusLost, EventShowStateChanged, EventKeyDown, EventKeyUp,
                                  EventMouseMove, EventMouseButton, EventMouseWheel, EventPaint, EventDragDropFile,
                                  EventWindowDestroying, EventTextInput, EventMouseLeave, EventMouseCaptureLost,
                                  EventPopupDismissed>;

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
    /// @par Thread safety
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
        /// Includes accepted registrations pending activation at the end of the outermost listener traversal.
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
