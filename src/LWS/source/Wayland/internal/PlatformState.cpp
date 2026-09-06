#ifdef LWS_PLATFORM_WAYLAND

    #include "PlatformState.hpp"

    #include "KeyCodeLinux.hpp"

    #include <LWS/Wayland/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cerrno>
    #include <cstring>
    #include <poll.h>
    #include <optional>
    #include <sys/eventfd.h>
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
    WaylandPlatformState& WaylandPlatformState::current()
    {
        static WaylandPlatformState state;
        return state;
    }

    Result WaylandPlatformState::initialize()
    {
        if (fInitCount != 0)
        {
            ++fInitCount;
            return Result::Success;
        }

        fDisplay = wl_display_connect(nullptr);
        if (fDisplay == nullptr)
        {
            return Result::Failure;
        }

        fWakeDescriptor = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (fWakeDescriptor < 0)
        {
            releaseObjects();
            return Result::Failure;
        }
        fKeyRepeatDescriptor = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);

        fRegistry = wl_display_get_registry(fDisplay);
        static constexpr wl_registry_listener registryListener{
            .global = registryGlobal,
            .global_remove = registryGlobalRemove,
        };
        wl_registry_add_listener(fRegistry, &registryListener, this);

        const bool registryReady = wl_display_roundtrip(fDisplay) >= 0 && wl_display_roundtrip(fDisplay) >= 0;
        if (!registryReady || fCompositor == nullptr || fSharedMemory == nullptr || fShell == nullptr)
        {
            releaseObjects();
            return Result::NotSupported;
        }

        fQuitRequested = false;
        fInitCount = 1;
        return Result::Success;
    }

    void WaylandPlatformState::shutdown()
    {
        if (fInitCount != 0)
        {
            --fInitCount;
            if (fInitCount == 0)
            {
                while (!fWindows.empty())
                {
                    fWindows.begin()->second.window->destroy();
                }
                releaseObjects();
            }
        }
    }

    bool WaylandPlatformState::isInitialized() const
    {
        return fInitCount != 0;
    }

    void WaylandPlatformState::runMessageLoop()
    {
        while (!fQuitRequested && fDisplay != nullptr)
        {
            dispatchOnce(-1);
        }
    }

    bool WaylandPlatformState::processMessages()
    {
        dispatchOnce(0);
        return fQuitRequested;
    }

    void WaylandPlatformState::requestQuit()
    {
        fQuitRequested = true;
        if (fWakeDescriptor >= 0)
        {
            const uint64_t value = 1;
            std::ignore = write(fWakeDescriptor, &value, sizeof(value));
        }
    }

    void WaylandPlatformState::postTask(std::move_only_function<void()> task)
    {
        {
            const std::scoped_lock lock(fTaskMutex);
            fTasks.push_back(std::move(task));
        }
        if (fWakeDescriptor >= 0)
        {
            const uint64_t value = 1;
            std::ignore = write(fWakeDescriptor, &value, sizeof(value));
        }
    }

    void WaylandPlatformState::registerWindow(wl_surface* surface, WindowBackendWayland& window,
                                              WaylandSurfaceRole role)
    {
        fWindows.emplace(surface, WindowRegistration{&window, role});
    }

    void WaylandPlatformState::unregisterWindow(wl_surface* surface)
    {
        WindowBackendWayland* window = findWindow(surface);
        if (fPointerWindow == window)
            fPointerWindow = nullptr;
        if (fKeyboardWindow == window)
            fKeyboardWindow = nullptr;
        fWindows.erase(surface);
        if (fWindows.empty())
        {
            requestQuit();
        }
    }

    WindowBackendWayland* WaylandPlatformState::findWindow(wl_surface* surface) const
    {
        const auto it = fWindows.find(surface);
        return it != fWindows.end() ? it->second.window : nullptr;
    }

    void WaylandPlatformState::applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible)
    {
        if (fPointerWindow == &window)
            fCursorController.apply(shape, visible, fPointer, fPointerEnterSerial, fCompositor, fSharedMemory);
    }

    bool WaylandPlatformState::isKeyPressed(KeyCode key) const
    {
        if (key == KeyCode::Shift)
        {
            return isKeyPressed(KeyCode::LShift) || isKeyPressed(KeyCode::RShift);
        }
        if (key == KeyCode::Control)
        {
            return isKeyPressed(KeyCode::LControl) || isKeyPressed(KeyCode::RControl);
        }
        if (key == KeyCode::Alt)
        {
            return isKeyPressed(KeyCode::LAlt) || isKeyPressed(KeyCode::RAlt);
        }
        return fPressedKeys.contains(key);
    }

    void WaylandPlatformState::registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                                              uint32_t version)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        if (std::strcmp(interface, wl_compositor_interface.name) == 0)
        {
            state.fCompositor = static_cast<wl_compositor*>(
                wl_registry_bind(registry, name, &wl_compositor_interface, std::min(version, 6U)));
        }
        else if (std::strcmp(interface, wl_subcompositor_interface.name) == 0)
        {
            state.fSubcompositor = static_cast<wl_subcompositor*>(
                wl_registry_bind(registry, name, &wl_subcompositor_interface, std::min(version, 1U)));
        }
        else if (std::strcmp(interface, wl_shm_interface.name) == 0)
        {
            state.fSharedMemory = static_cast<wl_shm*>(
                wl_registry_bind(registry, name, &wl_shm_interface, std::min(version, 1U)));
        }
        else if (std::strcmp(interface, xdg_wm_base_interface.name) == 0)
        {
            state.fShell = static_cast<xdg_wm_base*>(
                wl_registry_bind(registry, name, &xdg_wm_base_interface, std::min(version, 6U)));
            static constexpr xdg_wm_base_listener shellListener{.ping = shellPing};
            xdg_wm_base_add_listener(state.fShell, &shellListener, &state);
        }
        else if (std::strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        {
            state.fDecorationManager = static_cast<zxdg_decoration_manager_v1*>(
                wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, std::min(version, 1U)));
        }
        else if (std::strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0)
        {
            state.fPointerConstraints = static_cast<zwp_pointer_constraints_v1*>(
                wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1));
        }
        else if (std::strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0)
        {
            state.fRelativePointerManager = static_cast<zwp_relative_pointer_manager_v1*>(
                wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, 1));
        }
        else if (std::strcmp(interface, "weston_rdprail_shell") == 0)
        {
            state.fHasHostWindowFrame = true;
        }
        else if (std::strcmp(interface, wl_seat_interface.name) == 0 && state.fSeat == nullptr)
        {
            state.fSeat = static_cast<wl_seat*>(
                wl_registry_bind(registry, name, &wl_seat_interface, std::min(version, 9U)));
            static constexpr wl_seat_listener seatListener{
                .capabilities = seatCapabilities,
                .name = seatName,
            };
            wl_seat_add_listener(state.fSeat, &seatListener, &state);
        }
        else if (std::strcmp(interface, wl_output_interface.name) == 0)
        {
            state.fOutputManager.bindOutput(registry, name, version);
        }
    }

    void WaylandPlatformState::registryGlobalRemove(void* data, wl_registry*, uint32_t name)
    {
        static_cast<WaylandPlatformState*>(data)->fOutputManager.removeGlobal(name);
    }

    void WaylandPlatformState::shellPing(void*, xdg_wm_base* shell, uint32_t serial)
    {
        xdg_wm_base_pong(shell, serial);
    }

    void WaylandPlatformState::seatCapabilities(void* data, wl_seat* seat, uint32_t capabilities)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        if ((capabilities & WL_SEAT_CAPABILITY_POINTER) != 0 && state.fPointer == nullptr)
        {
            state.fPointer = wl_seat_get_pointer(seat);
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
            wl_pointer_add_listener(state.fPointer, &pointerListener, &state);
        }
        else if ((capabilities & WL_SEAT_CAPABILITY_POINTER) == 0 && state.fPointer != nullptr)
        {
            if (state.fPointerWindow != nullptr)
                std::ignore = state.fPointerWindow->setPointerLocked(false);
            wl_pointer_release(state.fPointer);
            state.fPointer = nullptr;
            state.fPointerWindow = nullptr;
        }

        if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0 && state.fKeyboard == nullptr)
        {
            state.fKeyboard = wl_seat_get_keyboard(seat);
            static constexpr wl_keyboard_listener keyboardListener{
                .keymap = keyboardKeymap,
                .enter = keyboardEnter,
                .leave = keyboardLeave,
                .key = keyboardKey,
                .modifiers = keyboardModifiers,
                .repeat_info = keyboardRepeatInfo,
            };
            wl_keyboard_add_listener(state.fKeyboard, &keyboardListener, &state);
        }
        else if ((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) == 0 && state.fKeyboard != nullptr)
        {
            wl_keyboard_release(state.fKeyboard);
            state.fKeyboard = nullptr;
            state.fKeyboardWindow = nullptr;
            state.fPressedKeys.clear();
            state.stopKeyRepeat();
        }
    }

    void WaylandPlatformState::seatName(void*, wl_seat*, const char*) {}

    void WaylandPlatformState::pointerEnter(void* data, wl_pointer*, uint32_t serial, wl_surface* surface, wl_fixed_t x,
                                            wl_fixed_t y)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        state.fPointerEnterSerial = serial;
        state.fPointerButtonSerial = 0;
        state.fPointerPosition = {wl_fixed_to_int(x), wl_fixed_to_int(y)};
        const auto registration = state.fWindows.find(surface);
        if (registration != state.fWindows.end())
        {
            state.fPointerWindow = registration->second.window;
            state.fPointerSurfaceRole = registration->second.role;
            state.fPointerWindow->handlePointerEnter(state.fPointerPosition, state.fPointerSurfaceRole);
        }
        else
        {
            state.fPointerWindow = nullptr;
            state.fPointerSurfaceRole = WaylandSurfaceRole::Content;
        }
    }

    void WaylandPlatformState::pointerLeave(void* data, wl_pointer*, uint32_t, wl_surface*)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        if (state.fPointerWindow != nullptr)
        {
            state.fPointerWindow->handlePointerLeave();
            state.fPointerWindow = nullptr;
        }
        state.fPointerSurfaceRole = WaylandSurfaceRole::Content;
        state.fPointerButtonSerial = 0;
        state.fPointerEnterSerial = 0;
    }

    void WaylandPlatformState::pointerMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        const Point position{wl_fixed_to_int(x), wl_fixed_to_int(y)};
        const Point delta{position.x - state.fPointerPosition.x, position.y - state.fPointerPosition.y};
        state.fPointerPosition = position;
        if (state.fPointerWindow != nullptr)
        {
            state.fPointerWindow->handlePointerMotion(position, delta, state.fPointerSurfaceRole);
        }
    }

    void WaylandPlatformState::pointerButton(void* data, wl_pointer*, uint32_t serial, uint32_t time, uint32_t button,
                                             uint32_t buttonState)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        state.fPointerButtonSerial = serial;
        const auto mouseButton = mouseButtonFromLinux(button);
        if (state.fPointerWindow != nullptr && mouseButton.has_value())
        {
            state.fPointerWindow->handlePointerButton(*mouseButton, buttonState == WL_POINTER_BUTTON_STATE_PRESSED,
                                                      state.fPointerPosition, state.fPointerSurfaceRole, time);
        }
    }

    void WaylandPlatformState::pointerAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        if (state.fPointerWindow != nullptr && axis == WL_POINTER_AXIS_VERTICAL_SCROLL)
        {
            state.fPointerWindow->handlePointerWheel(-wl_fixed_to_int(value) * 12, state.fPointerPosition);
        }
    }

    void WaylandPlatformState::pointerFrame(void*, wl_pointer*) {}
    void WaylandPlatformState::pointerAxisSource(void*, wl_pointer*, uint32_t) {}
    void WaylandPlatformState::pointerAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
    void WaylandPlatformState::pointerAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}
    void WaylandPlatformState::pointerAxisValue120(void*, wl_pointer*, uint32_t, int32_t) {}
    void WaylandPlatformState::pointerAxisRelativeDirection(void*, wl_pointer*, uint32_t, uint32_t) {}
    #ifdef WL_POINTER_WARP_SINCE_VERSION
    void WaylandPlatformState::pointerWarp(void* data, wl_pointer*, wl_fixed_t x, wl_fixed_t y)
    {
        pointerMotion(data, nullptr, 0, x, y);
    }
    #endif

    void WaylandPlatformState::keyboardKeymap(void*, wl_keyboard*, uint32_t, int32_t fd, uint32_t)
    {
        close(fd);
    }

    void WaylandPlatformState::keyboardEnter(void* data, wl_keyboard*, uint32_t, wl_surface* surface, wl_array*)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        state.fKeyboardWindow = state.findWindow(surface);
        if (state.fKeyboardWindow != nullptr)
        {
            state.fKeyboardWindow->handleKeyboardFocus(true);
        }
    }

    void WaylandPlatformState::keyboardLeave(void* data, wl_keyboard*, uint32_t, wl_surface*)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        if (state.fKeyboardWindow != nullptr)
        {
            state.fKeyboardWindow->handleKeyboardFocus(false);
            state.fKeyboardWindow = nullptr;
        }
        state.fPressedKeys.clear();
        state.stopKeyRepeat();
    }

    void WaylandPlatformState::keyboardKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key,
                                           uint32_t keyState)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        const KeyCode translated = keyCodeFromLinux(key);
        const bool pressed = keyState == WL_KEYBOARD_KEY_STATE_PRESSED;
        if (translated != KeyCode::Unknown)
        {
            if (pressed)
            {
                state.fPressedKeys.insert(translated);
            }
            else
            {
                state.fPressedKeys.erase(translated);
            }
        }
        if (state.fKeyboardWindow != nullptr)
        {
            state.fKeyboardWindow->handleKey(translated, pressed);
        }
        if (pressed && isRepeatableKey(translated))
            state.startKeyRepeat(translated);
        else if (translated == state.fRepeatingKey)
            state.stopKeyRepeat();
    }

    void WaylandPlatformState::keyboardModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t)
    {
    }
    void WaylandPlatformState::keyboardRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        state.fKeyRepeatRate = rate;
        state.fKeyRepeatDelay = delay;
        if (rate <= 0)
            state.stopKeyRepeat();
    }

    void WaylandPlatformState::dispatchTasks()
    {
        std::vector<std::move_only_function<void()>> tasks;
        {
            const std::scoped_lock lock(fTaskMutex);
            tasks.swap(fTasks);
        }
        for (auto& task : tasks)
        {
            task();
        }
    }

    void WaylandPlatformState::startKeyRepeat(KeyCode key)
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

    void WaylandPlatformState::stopKeyRepeat()
    {
        fRepeatingKey = KeyCode::Unknown;
        if (fKeyRepeatDescriptor >= 0)
        {
            const itimerspec timer{};
            std::ignore = timerfd_settime(fKeyRepeatDescriptor, 0, &timer, nullptr);
        }
    }

    void WaylandPlatformState::dispatchKeyRepeats()
    {
        uint64_t expirations{};
        if (fKeyRepeatDescriptor >= 0 && read(fKeyRepeatDescriptor, &expirations, sizeof(expirations)) > 0 &&
            fKeyboardWindow != nullptr && fPressedKeys.contains(fRepeatingKey))
        {
            for (uint64_t index = 0; index < expirations; ++index)
                fKeyboardWindow->handleKey(fRepeatingKey, true, true);
        }
    }

    void WaylandPlatformState::dispatchOnce(int timeoutMilliseconds)
    {
        if (fDisplay == nullptr)
        {
            fQuitRequested = true;
            return;
        }

        int result = wl_display_dispatch_pending(fDisplay);
        while (result >= 0 && wl_display_prepare_read(fDisplay) != 0)
        {
            result = wl_display_dispatch_pending(fDisplay);
        }
        if (result < 0)
        {
            fQuitRequested = true;
            return;
        }

        std::ignore = wl_display_flush(fDisplay);
        pollfd descriptors[]{
            {.fd = wl_display_get_fd(fDisplay), .events = POLLIN, .revents = 0},
            {.fd = fWakeDescriptor, .events = POLLIN, .revents = 0},
            {.fd = fKeyRepeatDescriptor, .events = POLLIN, .revents = 0},
        };
        const int pollResult = poll(descriptors, std::size(descriptors), timeoutMilliseconds);
        if (pollResult > 0 && (descriptors[0].revents & POLLIN) != 0)
        {
            if (wl_display_read_events(fDisplay) < 0)
            {
                fQuitRequested = true;
            }
        }
        else
        {
            wl_display_cancel_read(fDisplay);
        }

        if (pollResult > 0 && (descriptors[1].revents & POLLIN) != 0)
        {
            uint64_t value = 0;
            while (read(fWakeDescriptor, &value, sizeof(value)) > 0)
            {
            }
            dispatchTasks();
        }
        else if (pollResult < 0 && errno != EINTR)
        {
            fQuitRequested = true;
        }

        if (pollResult > 0 && (descriptors[2].revents & POLLIN) != 0)
            dispatchKeyRepeats();

        if (!fQuitRequested && wl_display_dispatch_pending(fDisplay) < 0)
        {
            fQuitRequested = true;
        }
    }

    void WaylandPlatformState::releaseObjects()
    {
        fCursorController.reset();
        fWindows.clear();
        fPressedKeys.clear();
        fPointerWindow = nullptr;
        fPointerSurfaceRole = WaylandSurfaceRole::Content;
        fPointerButtonSerial = 0;
        fPointerEnterSerial = 0;
        fKeyboardWindow = nullptr;
        if (fKeyboard != nullptr)
            wl_keyboard_release(fKeyboard);
        if (fPointer != nullptr)
            wl_pointer_release(fPointer);
        if (fSeat != nullptr)
            wl_seat_release(fSeat);
        fOutputManager.reset();
        if (fShell != nullptr)
            xdg_wm_base_destroy(fShell);
        if (fDecorationManager != nullptr)
            zxdg_decoration_manager_v1_destroy(fDecorationManager);
        if (fRelativePointerManager != nullptr)
            zwp_relative_pointer_manager_v1_destroy(fRelativePointerManager);
        if (fPointerConstraints != nullptr)
            zwp_pointer_constraints_v1_destroy(fPointerConstraints);
        if (fSharedMemory != nullptr)
            wl_shm_destroy(fSharedMemory);
        if (fSubcompositor != nullptr)
            wl_subcompositor_destroy(fSubcompositor);
        if (fCompositor != nullptr)
            wl_compositor_destroy(fCompositor);
        if (fRegistry != nullptr)
            wl_registry_destroy(fRegistry);
        if (fDisplay != nullptr)
            wl_display_disconnect(fDisplay);
        if (fWakeDescriptor >= 0)
            close(fWakeDescriptor);
        if (fKeyRepeatDescriptor >= 0)
            close(fKeyRepeatDescriptor);
        fKeyboard = nullptr;
        fPointer = nullptr;
        fSeat = nullptr;
        fShell = nullptr;
        fDecorationManager = nullptr;
        fRelativePointerManager = nullptr;
        fPointerConstraints = nullptr;
        fSharedMemory = nullptr;
        fSubcompositor = nullptr;
        fCompositor = nullptr;
        fRegistry = nullptr;
        fDisplay = nullptr;
        fWakeDescriptor = -1;
        fKeyRepeatDescriptor = -1;
        fKeyRepeatRate = 0;
        fKeyRepeatDelay = 0;
        fRepeatingKey = KeyCode::Unknown;
        {
            const std::scoped_lock lock(fTaskMutex);
            fTasks.clear();
        }
        fQuitRequested = false;
        fHasHostWindowFrame = false;
    }
}  // namespace LWS::internal

#endif
