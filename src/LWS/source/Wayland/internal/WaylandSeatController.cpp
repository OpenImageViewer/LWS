#ifdef LWS_PLATFORM_WAYLAND

    #include "WaylandSeatController.hpp"

    #include "KeyCodeLinux.hpp"
    #include "PlatformState.hpp"

    #include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cmath>
    #include <cstdlib>
    #include <sys/mman.h>
    #include <optional>
    #include <sys/timerfd.h>
    #include <tuple>
    #include <utility>
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
        if (seat != controller.fSeat)
            return;
        wl_surface* lostPointerSurface = nullptr;
        wl_surface* lostKeyboardSurface = nullptr;
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
            auto* pointer = std::exchange(controller.fPointer, nullptr);
            lostPointerSurface = controller.clearPointerFocus();
            if (auto* window = controller.fPlatform.findWindow(lostPointerSurface))
                std::ignore = window->setPointerLocked(false);
            if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
                wl_pointer_release(pointer);
            else
                wl_pointer_destroy(pointer);
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
            auto* keyboard = std::exchange(controller.fKeyboard, nullptr);
            lostKeyboardSurface = std::exchange(controller.fKeyboardSurface, nullptr);
            controller.fKeyboardWindow = nullptr;
            controller.clearKeymap();
            controller.fPressedScanCodes.clear();
            if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
                wl_keyboard_release(keyboard);
            else
                wl_keyboard_destroy(keyboard);
        }
        // All retiring resources and focus state are gone before any callback can
        // pump a capability change or replace the selected seat. No writes follow.
        controller.notifyFocusLoss(lostPointerSurface, lostKeyboardSurface);
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
        auto* surface = controller.clearPointerFocus();
        controller.notifyFocusLoss(surface, nullptr);
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
        controller.fInputSerial = serial;
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

    void WaylandSeatController::keyboardKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size)
    {
        auto& s = *static_cast<WaylandSeatController*>(data);
        // Every keymap event supersedes the old mapping, including no_keymap.
        s.clearKeymap();
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || size == 0)
        {
            close(fd);
            return;
        }
        void* mapped = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (mapped == MAP_FAILED)
            return;
        if (!s.fXkbContext)
            s.fXkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        auto* map = s.fXkbContext ? xkb_keymap_new_from_buffer(s.fXkbContext, static_cast<char*>(mapped), size - 1,
                                                               XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS)
                                  : nullptr;
        munmap(mapped, size);
        if (!map)
            return;
        auto* state = xkb_state_new(map);
        if (!state)
        {
            xkb_keymap_unref(map);
            return;
        }
        s.fXkbKeymap = map;
        s.fXkbState = state;
        if (!s.fComposeTable)
        {
            const char* locale = std::getenv("LC_ALL");
            if (!locale || !*locale)
                locale = std::getenv("LC_CTYPE");
            if (!locale || !*locale)
                locale = std::getenv("LANG");
            s.fComposeTable = xkb_compose_table_new_from_locale(s.fXkbContext, locale && *locale ? locale : "C.UTF-8",
                                                                XKB_COMPOSE_COMPILE_NO_FLAGS);
            if (s.fComposeTable)
                s.fComposeState = xkb_compose_state_new(s.fComposeTable, XKB_COMPOSE_STATE_NO_FLAGS);
        }
    }

    void WaylandSeatController::dispatchText(uint32_t key, bool repeat)
    {
        if (!fXkbState || !fKeyboardWindow)
            return;
        bool composed = false;
        const auto sym = xkb_state_key_get_one_sym(fXkbState, key + 8);
        // Repeating a printable prefix must not bypass an unfinished compose sequence.
        if (fComposeState && repeat && xkb_compose_state_get_status(fComposeState) == XKB_COMPOSE_COMPOSING)
            return;
        if (fComposeState && !repeat)
        {
            xkb_compose_state_feed(fComposeState, sym);
            auto status = xkb_compose_state_get_status(fComposeState);
            if (status == XKB_COMPOSE_COMPOSING)
                return;
            if (status == XKB_COMPOSE_CANCELLED)
            {
                xkb_compose_state_reset(fComposeState);
                return;
            }
            composed = status == XKB_COMPOSE_COMPOSED;
        }
        auto readText = [&](char* buffer, size_t size)
        {
            return composed ? xkb_compose_state_get_utf8(fComposeState, buffer, size)
                            : xkb_state_key_get_utf8(fXkbState, key + 8, buffer, size);
        };
        char text[128]{};
        int length = readText(text, sizeof(text));
        std::string committed;
        if (length >= int(sizeof(text)))
        {
            // Custom compose rules can produce strings larger than the normal stack buffer.
            committed.resize(size_t(length) + 1);
            length = readText(committed.data(), committed.size());
            committed.resize(length > 0 && size_t(length) < committed.size() ? size_t(length) : 0);
        }
        else if (length > 0)
            committed.assign(text, size_t(length));
        if (composed)
            xkb_compose_state_reset(fComposeState);
        // A completed non-text keysym consumes the sequence; it must not emit the raw final key.
        if (!committed.empty() && static_cast<unsigned char>(committed.front()) >= 32 && committed.front() != 127)
            fKeyboardWindow->handleText(std::move(committed));
    }

    void WaylandSeatController::keyboardEnter(void* data, wl_keyboard*, uint32_t, wl_surface* surface, wl_array* keys)
    {
        auto& state = *static_cast<WaylandSeatController*>(data);
        state.resetKeyboardInput();
        state.fKeyboardSurface = surface;
        if (keys)
        {
            const auto* codes = static_cast<const uint32_t*>(keys->data);
            for (size_t index = 0; index < keys->size / sizeof(uint32_t); ++index)
                state.fPressedScanCodes.insert(codes[index]);
        }
        state.fKeyboardWindow = state.fPlatform.findWindow(surface);
        if (state.fKeyboardWindow != nullptr)
        {
            state.fKeyboardWindow->handleKeyboardFocus(true);
        }
    }

    void WaylandSeatController::keyboardLeave(void* data, wl_keyboard*, uint32_t, wl_surface*)
    {
        auto& state = *static_cast<WaylandSeatController*>(data);
        auto* window = state.fKeyboardWindow;
        state.fKeyboardWindow = nullptr;
        state.fKeyboardSurface = nullptr;
        state.resetKeyboardInput();
        if (window)
            window->handleKeyboardFocus(false);
    }

    void WaylandSeatController::keyboardKey(void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key,
                                            uint32_t keyState)
    {
        auto& state = *static_cast<WaylandSeatController*>(data);
        state.fInputSerial = serial;
        const uint64_t generation = ++state.fKeyboardGeneration;
        auto* window = state.fKeyboardWindow;
        const KeyCode translated = keyCodeFromLinux(key);
        const bool pressed = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (pressed)
            state.fPressedScanCodes.insert(key);
        else
            state.fPressedScanCodes.erase(key);
        if (window)
            window->handleKey(translated, pressed);
        // A key handler can pump a modal loop, release this key, or destroy its window.
        if (!state.keyboardDispatchCurrent(window, generation))
            return;
        if (pressed)
            state.dispatchText(key);
        if (!state.keyboardDispatchCurrent(window, generation))
            return;
        if (pressed)
            state.startKeyRepeat(key);
        else if (state.fRepeatingScanCode == key)
            state.stopKeyRepeat();
    }

    void WaylandSeatController::keyboardModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed,
                                                  uint32_t latched, uint32_t locked, uint32_t group)
    {
        auto& s = *static_cast<WaylandSeatController*>(data);
        ++s.fKeyboardGeneration;
        if (s.fXkbState)
            xkb_state_update_mask(s.fXkbState, depressed, latched, locked, 0, 0, group);
    }
    void WaylandSeatController::keyboardRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay)
    {
        auto& state = *static_cast<WaylandSeatController*>(data);
        state.fKeyRepeatRate = rate;
        state.fKeyRepeatDelay = delay;
        if (rate <= 0)
            state.stopKeyRepeat();
    }

    bool WaylandSeatController::isKeyPressed(KeyCode key) const
    {
        if (key == KeyCode::Shift)
            return isKeyPressed(KeyCode::LShift) || isKeyPressed(KeyCode::RShift);
        if (key == KeyCode::Control)
            return isKeyPressed(KeyCode::LControl) || isKeyPressed(KeyCode::RControl);
        if (key == KeyCode::Alt)
            return isKeyPressed(KeyCode::LAlt) || isKeyPressed(KeyCode::RAlt);
        return key != KeyCode::Unknown && std::ranges::any_of(fPressedScanCodes, [key](uint32_t scanCode)
                                                              { return keyCodeFromLinux(scanCode) == key; });
    }

    void WaylandSeatController::applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible)
    {
        if (fPointerWindow == &window)
        {
            fCursorController.apply(shape, visible, fPointer, fPointerEnterSerial, fPlatform.compositor(),
                                    fPlatform.sharedMemory(),
                                    std::max(1, static_cast<int32_t>(std::ceil(window.contentScale()))));
        }
    }

    bool WaylandSeatController::keyboardDispatchCurrent(WindowBackendWayland* window, uint64_t generation) const
    {
        return fPlatform.context().IsUsable() && window && fKeyboardWindow == window &&
               fKeyboardGeneration == generation && window->surface() != nullptr;
    }

    void WaylandSeatController::resetCompositionAndRepeat()
    {
        ++fKeyboardGeneration;
        if (fComposeState)
            xkb_compose_state_reset(fComposeState);
        stopKeyRepeat();
    }

    void WaylandSeatController::resetKeyboardInput()
    {
        resetCompositionAndRepeat();
        fPressedScanCodes.clear();
    }

    void WaylandSeatController::clearKeymap()
    {
        // Mapping changes do not release physically held keys.
        resetCompositionAndRepeat();
        xkb_state_unref(fXkbState);
        fXkbState = nullptr;
        xkb_keymap_unref(fXkbKeymap);
        fXkbKeymap = nullptr;
    }

    bool WaylandSeatController::isRepeatableScanCode(uint32_t scanCode) const
    {
        return fXkbKeymap ? xkb_keymap_key_repeats(fXkbKeymap, scanCode + 8) != 0
                          : isRepeatableKey(keyCodeFromLinux(scanCode));
    }

    void WaylandSeatController::startKeyRepeat(uint32_t scanCode)
    {
        if (!isRepeatableScanCode(scanCode))
            return;
        stopKeyRepeat();
        if (fKeyRepeatDescriptor < 0 || fKeyRepeatRate <= 0 || fKeyRepeatDelay < 0)
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
        {
            fRepeatingScanCode = scanCode;
        }
    }

    void WaylandSeatController::stopKeyRepeat()
    {
        fRepeatingScanCode.reset();
        if (fKeyRepeatDescriptor >= 0)
        {
            const itimerspec timer{};
            std::ignore = timerfd_settime(fKeyRepeatDescriptor, 0, &timer, nullptr);
        }
    }

    void WaylandSeatController::dispatchKeyRepeats()
    {
        uint64_t expirations{};
        if (fKeyRepeatDescriptor >= 0 && read(fKeyRepeatDescriptor, &expirations, sizeof(expirations)) > 0)
            dispatchKeyRepeats(expirations);
    }

    void WaylandSeatController::dispatchKeyRepeats(uint64_t expirations)
    {
        auto* window = fKeyboardWindow;
        const auto scanCode = fRepeatingScanCode;
        if (!scanCode)
            return;
        const auto key = keyCodeFromLinux(*scanCode);
        const auto generation = fKeyboardGeneration;
        auto current = [&]
        {
            return keyboardDispatchCurrent(window, generation) && fRepeatingScanCode == scanCode &&
                   fPressedScanCodes.contains(*scanCode);
        };
        for (uint64_t index = 0; index < expirations && current(); ++index)
        {
            window->handleKey(key, true, true);
            if (!current())
                break;
            dispatchText(*scanCode, true);
        }
    }

    void WaylandSeatController::beginPointerWheelFrame()
    {
        if (fPointerWheelWindow == nullptr)
        {
            fPointerWheelWindow = fPointerWindow;
            fPointerWheelPosition = fPointerPosition;
            fPointerWheelRole = fPointerSurfaceRole;
        }
    }

    void WaylandSeatController::dispatchPointerWheelFrame()
    {
        const std::optional<int32_t> delta = fPointerWheelFrame.takeDelta();
        auto* window = std::exchange(fPointerWheelWindow, nullptr);
        const auto position = fPointerWheelPosition;
        const auto role = fPointerWheelRole;
        if (delta && window)
            window->handlePointerWheel(*delta, position, role);
    }

    void WaylandSeatController::windowRemoved(WindowBackendWayland& window)
    {
        if (fPointerWindow == &window)
            std::ignore = clearPointerFocus();
        if (fPointerWheelWindow == &window)
        {
            fPointerWheelFrame.clear();
            fPointerWheelWindow = nullptr;
        }
        if (fKeyboardWindow == &window)
        {
            fKeyboardWindow = nullptr;
            fKeyboardSurface = nullptr;
            resetKeyboardInput();
        }
    }

    wl_surface* WaylandSeatController::clearPointerFocus()
    {
        auto* window = std::exchange(fPointerWindow, nullptr);
        auto* surface = window ? static_cast<wl_surface*>(window->surface()) : nullptr;
        fPointerSurfaceRole = WaylandSurfaceRole::Content;
        fPointerButtonSerial = 0;
        fPointerEnterSerial = 0;
        fPointerWheelFrame.clear();
        fPointerWheelWindow = nullptr;
        fPointerWheelPosition = {};
        return surface;
    }

    void WaylandSeatController::notifyFocusLoss(wl_surface* pointerSurface, wl_surface* keyboardSurface)
    {
        if (auto* window = fPlatform.findWindow(pointerSurface); window && window != fPointerWindow)
            window->handlePointerLeave();
        // The first callback can destroy this target or give it focus on a new seat.
        if (auto* window = fPlatform.findWindow(keyboardSurface); window && window != fKeyboardWindow)
            window->handleKeyboardFocus(false);
    }

    void WaylandSeatController::reset()
    {
        auto* pointerSurface = clearPointerFocus();
        auto* keyboardSurface = std::exchange(fKeyboardSurface, nullptr);
        fKeyboardWindow = nullptr;
        if (auto* window = fPlatform.findWindow(pointerSurface))
            std::ignore = window->setPointerLocked(false);
        fCursorController.reset();
        clearKeymap();
        fPressedScanCodes.clear();
        xkb_compose_state_unref(fComposeState);
        fComposeState = nullptr;
        xkb_compose_table_unref(fComposeTable);
        fComposeTable = nullptr;
        xkb_context_unref(fXkbContext);
        fXkbContext = nullptr;
        fInputSerial = 0;
        fPointerPosition = {};
        if (auto* keyboard = std::exchange(fKeyboard, nullptr))
        {
            if (wl_keyboard_get_version(keyboard) >= WL_KEYBOARD_RELEASE_SINCE_VERSION)
                wl_keyboard_release(keyboard);
            else
                wl_keyboard_destroy(keyboard);
        }
        if (auto* pointer = std::exchange(fPointer, nullptr))
        {
            if (wl_pointer_get_version(pointer) >= WL_POINTER_RELEASE_SINCE_VERSION)
                wl_pointer_release(pointer);
            else
                wl_pointer_destroy(pointer);
        }
        if (auto* seat = std::exchange(fSeat, nullptr))
        {
            if (wl_seat_get_version(seat) >= WL_SEAT_RELEASE_SINCE_VERSION)
                wl_seat_release(seat);
            else
                wl_seat_destroy(seat);
        }
        if (fKeyRepeatDescriptor >= 0)
            close(fKeyRepeatDescriptor);
        fKeyRepeatDescriptor = -1;
        fKeyRepeatRate = 0;
        fKeyRepeatDelay = 0;
        notifyFocusLoss(pointerSurface, keyboardSurface);
    }
}  // namespace LWS::internal

#endif
