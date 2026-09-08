#ifdef LWS_PLATFORM_WAYLAND

    #include "WaylandSeatController.hpp"

    #include "KeyCodeLinux.hpp"
    #include "PlatformState.hpp"

    #include <LWS/Wayland/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cmath>
    #include <optional>
    #include <sys/timerfd.h>
    #include <tuple>
    #include <unistd.h>

namespace
{
    std::optional<LWS::MouseButton> mouseButtonFromLinux(uint32_t button)
    {
        using LWS::MouseButton;
        switch (button)
        {
            case BTN_LEFT:
                return MouseButton::Left;
            case BTN_MIDDLE:
                return MouseButton::Middle;
            case BTN_RIGHT:
                return MouseButton::Right;
            case BTN_SIDE:
            case BTN_BACK:
                return MouseButton::X1;
            case BTN_EXTRA:
            case BTN_FORWARD:
                return MouseButton::X2;
            default:
                return std::nullopt;
        }
    }

    bool isRepeatableKey(LWS::KeyCode key)
    {
        using LWS::KeyCode;
        return key != KeyCode::Unknown && key != KeyCode::Shift && key != KeyCode::Control && key != KeyCode::Alt &&
               key != KeyCode::Win && key != KeyCode::LShift && key != KeyCode::RShift && key != KeyCode::LControl &&
               key != KeyCode::RControl && key != KeyCode::LAlt && key != KeyCode::RAlt && key != KeyCode::CapsLock &&
               key != KeyCode::NumLock && key != KeyCode::ScrollLock;
    }
}  // namespace

namespace LWS::internal
{
    WaylandSeatController::WaylandSeatController(WaylandPlatformState& platform) : fPlatform(platform) {}

    void WaylandSeatController::initialize()
    {
        if (fKeyRepeatDescriptor < 0)
            fKeyRepeatDescriptor = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
    }

    void WaylandSeatController::bindSeat(wl_registry* registry, uint32_t name, uint32_t version)
    {
        if (fSeat != nullptr)
            return;
        initialize();
        fSeat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 9U)));
        static constexpr wl_seat_listener seatListener{
            .capabilities = seatCapabilities,
            .name = seatName,
        };
        wl_seat_add_listener(fSeat, &seatListener, this);
    }

    void WaylandSeatController::seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && controller.fPointer == nullptr)
        {
            controller.fPointer = wl_seat_get_pointer(seat);
            static constexpr wl_pointer_listener pointerListener{
                .enter = pointerEnter,
                .leave = pointerLeave,
                .motion = pointerMotion,
                .button = pointerButton,
                .axis = pointerAxis,
                .frame = pointerFrame,
                .axis_source = pointerAxisSource,
                .axis_stop = pointerAxisStop,
                .axis_discrete = pointerAxisDiscrete,
                .axis_value120 = pointerAxisValue120,
                .axis_relative_direction = pointerAxisRelativeDirection,
    #ifdef WL_POINTER_WARP_SINCE_VERSION
                .warp = pointerWarp,
    #endif
            };
            wl_pointer_add_listener(controller.fPointer, &pointerListener, &controller);
        }
        else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && controller.fPointer != nullptr)
        {
            if (controller.fPointerWindow != nullptr)
            {
                std::ignore = controller.fPointerWindow->setPointerLocked(false);
                controller.fPointerWindow->handlePointerLeave();
            }
            if (wl_pointer_get_version(controller.fPointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
                wl_pointer_release(controller.fPointer);
            else
                wl_pointer_destroy(controller.fPointer);
            controller.fPointer = nullptr;
            controller.fPointerWindow = nullptr;
            controller.fPointerWheelFrame.clear();
            controller.fPointerWheelWindow = nullptr;
        }

        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && controller.fKeyboard == nullptr)
        {
            controller.fKeyboard = wl_seat_get_keyboard(seat);
            static constexpr wl_keyboard_listener keyboardListener{
                .keymap = keyboardKeymap,
                .enter = keyboardEnter,
                .leave = keyboardLeave,
                .key = keyboardKey,
                .modifiers = keyboardModifiers,
                .repeat_info = keyboardRepeatInfo,
            };
            wl_keyboard_add_listener(controller.fKeyboard, &keyboardListener, &controller);
        }
        else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && controller.fKeyboard != nullptr)
        {
            if (controller.fKeyboardWindow != nullptr)
                controller.fKeyboardWindow->handleKeyboardFocus(false);
            if (wl_keyboard_get_version(controller.fKeyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
                wl_keyboard_release(controller.fKeyboard);
            else
                wl_keyboard_destroy(controller.fKeyboard);
            controller.fKeyboard = nullptr;
            controller.fKeyboardWindow = nullptr;
            controller.fPressedKeys.clear();
            controller.stopKeyRepeat();
        }
    }

    void WaylandSeatController::seatName(void*, wl_seat*, const char*) {}

    void WaylandSeatController::pointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface* surface,
                                             wl_fixed_t x, wl_fixed_t y)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        controller.fPointerEnterSerial = serial;
        controller.fPointerButtonSerial = 0;
        controller.fPointerPosition = {wl_fixed_to_int(x), wl_fixed_to_int(y)};
        const WaylandWindowRegistration* registration = controller.fPlatform.findWindowRegistration(surface);
        if (registration != nullptr)
        {
            controller.fPointerWindow = registration->window;
            controller.fPointerSurfaceRole = registration->role;
            controller.fPointerWindow->handlePointerEnter(controller.fPointerPosition, controller.fPointerSurfaceRole);
        }
        else
        {
            controller.fPointerWindow = nullptr;
            controller.fPointerSurfaceRole = WaylandSurfaceRole::Content;
        }
    }

    void WaylandSeatController::pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if (controller.fPointerWindow != nullptr)
        {
            controller.fPointerWindow->handlePointerLeave();
            controller.fPointerWindow = nullptr;
        }
        controller.fPointerSurfaceRole = WaylandSurfaceRole::Content;
        controller.fPointerButtonSerial = 0;
        controller.fPointerEnterSerial = 0;
    }

    void WaylandSeatController::pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        const Point position{wl_fixed_to_int(x), wl_fixed_to_int(y)};
        const Point delta{position.x - controller.fPointerPosition.x, position.y - controller.fPointerPosition.y};
        controller.fPointerPosition = position;
        if (controller.fPointerWindow != nullptr)
            controller.fPointerWindow->handlePointerMotion(position, delta, controller.fPointerSurfaceRole);
    }

    void WaylandSeatController::pointerButton(void* data, wl_pointer*, uint32_t serial, uint32_t time, uint32_t button,
                                              uint32_t buttonState)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        controller.fPointerButtonSerial = serial;
        const auto mouseButton = mouseButtonFromLinux(button);
        if (controller.fPointerWindow != nullptr && mouseButton.has_value())
        {
            controller.fPointerWindow->handlePointerButton(*mouseButton, buttonState == WL_POINTER_BUTTON_STATE_PRESSED,
                                                           controller.fPointerPosition, controller.fPointerSurfaceRole,
                                                           time);
        }
    }

    void WaylandSeatController::pointerAxis(void* data, wl_pointer* pointer, uint32_t, uint32_t axis, wl_fixed_t value)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if (controller.fPointerWindow != nullptr && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            controller.beginPointerWheelFrame();
            controller.fPointerWheelFrame.addWaylandAxis(wl_fixed_to_double(value));
            if (!WheelDeltaFrame::usesPointerFrame(wl_pointer_get_version(pointer)))
                controller.dispatchPointerWheelFrame();
        }
    }

    void WaylandSeatController::pointerFrame(void* data, wl_pointer*)
    {
        static_cast<WaylandSeatController*>(data)->dispatchPointerWheelFrame();
    }

    void WaylandSeatController::pointerAxisSource(void*, wl_pointer*, uint32_t) {}
    void WaylandSeatController::pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}

    void WaylandSeatController::pointerAxisDiscrete(void* data, wl_pointer*, uint32_t axis, int32_t discrete)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if (controller.fPointerWindow != nullptr && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            controller.beginPointerWheelFrame();
            controller.fPointerWheelFrame.addWaylandDiscrete(discrete);
        }
    }

    void WaylandSeatController::pointerAxisValue120(void* data, wl_pointer*, uint32_t axis, int32_t value120)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if (controller.fPointerWindow != nullptr && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            controller.beginPointerWheelFrame();
            controller.fPointerWheelFrame.addWaylandValue120(value120);
        }
    }

    void WaylandSeatController::pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}

    #ifdef WL_POINTER_WARP_SINCE_VERSION
    void WaylandSeatController::pointerWarp(void* data, wl_pointer*, wl_fixed_t x, wl_fixed_t y)
    {
        pointerMotion(data, nullptr, 0, x, y);
    }
    #endif

    void WaylandSeatController::keyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t)
    {
        close(fd);
    }

    void WaylandSeatController::keyboardEnter(void* data, wl_keyboard*, uint32_t, wl_surface* surface, wl_array*)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        controller.fKeyboardWindow = controller.fPlatform.findWindow(surface);
        if (controller.fKeyboardWindow != nullptr)
            controller.fKeyboardWindow->handleKeyboardFocus(true);
    }

    void WaylandSeatController::keyboardLeave(void* data, wl_keyboard*, uint32_t, wl_surface*)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        if (controller.fKeyboardWindow != nullptr)
        {
            controller.fKeyboardWindow->handleKeyboardFocus(false);
            controller.fKeyboardWindow = nullptr;
        }
        controller.fPressedKeys.clear();
        controller.stopKeyRepeat();
    }

    void WaylandSeatController::keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                                            uint32_t keyState)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        const KeyCode translated = keyCodeFromLinux(key);
        const bool pressed = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (translated != KeyCode::Unknown)
        {
            if (pressed)
                controller.fPressedKeys.insert(translated);
            else
                controller.fPressedKeys.erase(translated);
        }
        if (controller.fKeyboardWindow != nullptr)
            controller.fKeyboardWindow->handleKey(translated, pressed);
        if (pressed && isRepeatableKey(translated))
            controller.startKeyRepeat(translated);
        else if (translated == controller.fRepeatingKey)
            controller.stopKeyRepeat();
    }

    void WaylandSeatController::keyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
    {
    }

    void WaylandSeatController::keyboardRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay)
    {
        auto& controller = *static_cast<WaylandSeatController*>(data);
        controller.fKeyRepeatRate = rate;
        controller.fKeyRepeatDelay = delay;
        if (rate <= 0)
            controller.stopKeyRepeat();
    }

    bool WaylandSeatController::isKeyPressed(KeyCode key) const
    {
        if (key == KeyCode::Shift)
            return isKeyPressed(KeyCode::LShift) || isKeyPressed(KeyCode::RShift);
        if (key == KeyCode::Control)
            return isKeyPressed(KeyCode::LControl) || isKeyPressed(KeyCode::RControl);
        if (key == KeyCode::Alt)
            return isKeyPressed(KeyCode::LAlt) || isKeyPressed(KeyCode::RAlt);
        return fPressedKeys.contains(key);
    }

    void WaylandSeatController::applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible)
    {
        if (fPointerWindow == &window)
        {
            fCursorController.apply(shape, visible, fPointer, fPointerEnterSerial, fPlatform.compositor(),
                                    fPlatform.sharedMemory());
        }
    }

    void WaylandSeatController::startKeyRepeat(KeyCode key)
    {
        stopKeyRepeat();
        if (fKeyRepeatDescriptor < 0 || fKeyRepeatRate <= 0 || fKeyRepeatDelay < 0 || !isRepeatableKey(key))
            return;

        constexpr int64_t nanosecondsPerSecond = 1'000'000'000;
        constexpr int64_t nanosecondsPerMillisecond = 1'000'000;
        const int64_t interval = nanosecondsPerSecond / fKeyRepeatRate;
        const itimerspec timer{
            .it_interval = {.tv_sec = interval / nanosecondsPerSecond, .tv_nsec = interval % nanosecondsPerSecond},
            .it_value = {.tv_sec = fKeyRepeatDelay / 1000,
                         .tv_nsec = static_cast<int64_t>(fKeyRepeatDelay % 1000) * nanosecondsPerMillisecond},
        };
        if (timerfd_settime(fKeyRepeatDescriptor, 0, &timer, nullptr) == 0)
            fRepeatingKey = key;
    }

    void WaylandSeatController::stopKeyRepeat()
    {
        fRepeatingKey = KeyCode::Unknown;
        if (fKeyRepeatDescriptor >= 0)
        {
            const itimerspec timer{};
            std::ignore = timerfd_settime(fKeyRepeatDescriptor, 0, &timer, nullptr);
        }
    }

    void WaylandSeatController::dispatchKeyRepeats()
    {
        uint64_t expirations{};
        if (fKeyRepeatDescriptor >= 0 && read(fKeyRepeatDescriptor, &expirations, sizeof(expirations)) > 0 &&
            fKeyboardWindow != nullptr && fPressedKeys.contains(fRepeatingKey))
        {
            WindowBackendWayland* window = fKeyboardWindow;
            const KeyCode key = fRepeatingKey;
            for (uint64_t index = 0; index < expirations && fKeyboardWindow == window && fRepeatingKey == key &&
                                     fPressedKeys.contains(key); ++index)
            {
                window->handleKey(key, true, true);
            }
        }
    }

    void WaylandSeatController::beginPointerWheelFrame()
    {
        if (fPointerWheelWindow == nullptr)
        {
            fPointerWheelWindow = fPointerWindow;
            fPointerWheelPosition = fPointerPosition;
        }
    }

    void WaylandSeatController::dispatchPointerWheelFrame()
    {
        const std::optional<int32_t> delta = fPointerWheelFrame.takeDelta();
        if (delta && fPointerWheelWindow != nullptr)
            fPointerWheelWindow->handlePointerWheel(*delta, fPointerWheelPosition);
        fPointerWheelWindow = nullptr;
    }

    void WaylandSeatController::windowRemoved(WindowBackendWayland& window)
    {
        if (fPointerWindow == &window)
            fPointerWindow = nullptr;
        if (fPointerWheelWindow == &window)
        {
            fPointerWheelFrame.clear();
            fPointerWheelWindow = nullptr;
        }
        if (fKeyboardWindow == &window)
        {
            fKeyboardWindow = nullptr;
            fPressedKeys.clear();
            stopKeyRepeat();
        }
    }

    void WaylandSeatController::reset()
    {
        if (fPointerWindow != nullptr)
        {
            std::ignore = fPointerWindow->setPointerLocked(false);
            fPointerWindow->handlePointerLeave();
        }
        if (fKeyboardWindow != nullptr)
            fKeyboardWindow->handleKeyboardFocus(false);
        fCursorController.reset();
        stopKeyRepeat();
        fPressedKeys.clear();
        fPointerWindow = nullptr;
        fPointerSurfaceRole = WaylandSurfaceRole::Content;
        fKeyboardWindow = nullptr;
        fPointerButtonSerial = 0;
        fPointerEnterSerial = 0;
        fPointerPosition = {};
        fPointerWheelFrame.clear();
        fPointerWheelWindow = nullptr;
        fPointerWheelPosition = {};
        if (fKeyboard != nullptr)
        {
            if (wl_keyboard_get_version(fKeyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
                wl_keyboard_release(fKeyboard);
            else
                wl_keyboard_destroy(fKeyboard);
        }
        if (fPointer != nullptr)
        {
            if (wl_pointer_get_version(fPointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
                wl_pointer_release(fPointer);
            else
                wl_pointer_destroy(fPointer);
        }
        if (fSeat != nullptr)
        {
            if (wl_seat_get_version(fSeat) >= WL_SEAT_RELEASE_SINCE_VERSION)
                wl_seat_release(fSeat);
            else
                wl_seat_destroy(fSeat);
        }
        if (fKeyRepeatDescriptor >= 0)
            close(fKeyRepeatDescriptor);
        fKeyboard = nullptr;
        fPointer = nullptr;
        fSeat = nullptr;
        fKeyRepeatDescriptor = -1;
        fKeyRepeatRate = 0;
        fKeyRepeatDelay = 0;
        fRepeatingKey = KeyCode::Unknown;
    }
}  // namespace LWS::internal

#endif
