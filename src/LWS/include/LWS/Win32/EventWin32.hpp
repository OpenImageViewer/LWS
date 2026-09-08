#pragma once
#ifdef LWS_HAS_WIN32_BACKEND

    #include <LWS/Event.hpp>
    #include <LWS/WindowTypes.hpp>

    #include <Windows.h>

    #include <cstddef>
    #include <cstdint>
    #include <functional>
    #include <optional>
    #include <span>
    #include <variant>

namespace LWS::Win32
{
    struct PaintEvent
    {
        // Valid only for the duration of the PlatformCallback invocation.
        HDC deviceContext;
        Rect invalidRect;
    };

    enum class VerticalScrollAction
    {
        PageUp,
        PageDown,
        ThumbPosition,
        ThumbTrack,
    };

    struct VerticalScrollEvent
    {
        VerticalScrollAction action;
        int32_t position;
    };

    struct KeyEvent
    {
        uint32_t virtualKey;
        uint32_t keyData;
        bool pressed;
    };

    struct ActivationEvent
    {
        bool active;
    };

    struct CopyDataEvent
    {
        uintptr_t identifier;
        // References the synchronous WM_COPYDATA payload and is valid only during the callback.
        std::span<const std::byte> data;
    };

    struct NotificationIconEvent
    {
        static constexpr uint32_t MessageId = WM_USER + 1;

        uint32_t notification;
        int16_t x;
        int16_t y;
    };

    using PlatformEvent =
        std::variant<PaintEvent, VerticalScrollEvent, KeyEvent, ActivationEvent, CopyDataEvent, NotificationIconEvent>;
    // A result handles the native message; nullopt continues LWS and Win32 default handling.
    using PlatformCallback = std::move_only_function<std::optional<LRESULT>(const PlatformEvent&)>;
}  // namespace LWS::Win32
#endif
