#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include "WaylandCursorController.hpp"
    #include "WheelDeltaFrame.hpp"
    #include "WindowFrame.hpp"

    #include <LWS/KeyCode.hpp>
    #include <LWS/source/internal/Backends.hpp>

    #include <cstdint>
    #include <unordered_set>

    #include <wayland-client.h>

namespace LWS
{
    class WindowBackendWayland;
}

namespace LWS::internal
{
    class WaylandPlatformState;

    class WaylandSeatController
    {
      public:

        explicit WaylandSeatController(WaylandPlatformState& platform);

        void initialize();
        void bindSeat(wl_registry* registry, uint32_t name, uint32_t version);
        [[nodiscard]] wl_seat* seat() const { return fSeat; }
        [[nodiscard]] wl_pointer* pointer() const { return fPointer; }
        [[nodiscard]] uint32_t pointerButtonSerial() const { return fPointerButtonSerial; }
        [[nodiscard]] Point pointerPosition() const { return fPointerPosition; }
        [[nodiscard]] bool isKeyPressed(KeyCode key) const;
        [[nodiscard]] int pollDescriptor() const { return fKeyRepeatDescriptor; }
        void applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible);
        void dispatchKeyRepeats();
        void windowRemoved(WindowBackendWayland& window);
        void reset();

      private:

        static void seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
        static void seatName(void* data, wl_seat* seat, const char* name);
        static void pointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, wl_fixed_t x,
                                 wl_fixed_t y);
        static void pointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
        static void pointerMotion(void* data, wl_pointer* pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y);
        static void pointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button,
                                  uint32_t state);
        static void pointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, wl_fixed_t value);
        static void pointerFrame(void* data, wl_pointer* pointer);
        static void pointerAxisSource(void* data, wl_pointer* pointer, uint32_t source);
        static void pointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
        static void pointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
        static void pointerAxisValue120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120);
        static void pointerAxisRelativeDirection(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction);
    #ifdef WL_POINTER_WARP_SINCE_VERSION
        static void pointerWarp(void* data, wl_pointer* pointer, wl_fixed_t x, wl_fixed_t y);
    #endif
        static void keyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
        static void keyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface,
                                  wl_array* keys);
        static void keyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
        static void keyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key,
                                uint32_t state);
        static void keyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t depressed,
                                      uint32_t latched, uint32_t locked, uint32_t group);
        static void keyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);

        void beginPointerWheelFrame();
        void dispatchPointerWheelFrame();
        void startKeyRepeat(KeyCode key);
        void stopKeyRepeat();

        WaylandPlatformState& fPlatform;
        wl_seat* fSeat = nullptr;
        wl_pointer* fPointer = nullptr;
        wl_keyboard* fKeyboard = nullptr;
        int fKeyRepeatDescriptor = -1;
        int32_t fKeyRepeatRate = 0;
        int32_t fKeyRepeatDelay = 0;
        KeyCode fRepeatingKey = KeyCode::Unknown;
        WaylandCursorController fCursorController;
        WindowBackendWayland* fPointerWindow = nullptr;
        WaylandSurfaceRole fPointerSurfaceRole = WaylandSurfaceRole::Content;
        WindowBackendWayland* fKeyboardWindow = nullptr;
        uint32_t fPointerButtonSerial = 0;
        uint32_t fPointerEnterSerial = 0;
        Point fPointerPosition{};
        WheelDeltaFrame fPointerWheelFrame;
        WindowBackendWayland* fPointerWheelWindow = nullptr;
        Point fPointerWheelPosition{};
        std::unordered_set<KeyCode> fPressedKeys;
    };
}  // namespace LWS::internal

#endif
