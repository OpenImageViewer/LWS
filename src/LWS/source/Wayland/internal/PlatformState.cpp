#ifdef LWS_PLATFORM_WAYLAND

    #include "PlatformState.hpp"

    #include <LWS/Wayland/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cerrno>
    #include <cstring>
    #include <poll.h>
    #include <sys/eventfd.h>
    #include <tuple>
    #include <unistd.h>

namespace LWS::internal
{
    WaylandPlatformState::WaylandPlatformState() : fSeatController(*this) {}

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
            return Result::Failure;

        fWakeDescriptor = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (fWakeDescriptor < 0)
        {
            releaseObjects();
            return Result::Failure;
        }
        fSeatController.initialize();

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
                    fWindows.begin()->second.window->destroy();
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
            dispatchOnce(-1);
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
        fWindows.emplace(surface, WaylandWindowRegistration{&window, role});
    }

    void WaylandPlatformState::unregisterWindow(wl_surface* surface)
    {
        if (WindowBackendWayland* window = findWindow(surface); window != nullptr)
            fSeatController.windowRemoved(*window);
        fWindows.erase(surface);
        if (fWindows.empty())
            requestQuit();
    }

    const WaylandWindowRegistration* WaylandPlatformState::findWindowRegistration(wl_surface* surface) const
    {
        const auto it = fWindows.find(surface);
        return it != fWindows.end() ? &it->second : nullptr;
    }

    WindowBackendWayland* WaylandPlatformState::findWindow(wl_surface* surface) const
    {
        const WaylandWindowRegistration* registration = findWindowRegistration(surface);
        return registration != nullptr ? registration->window : nullptr;
    }

    void WaylandPlatformState::applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible)
    {
        fSeatController.applyCursor(window, shape, visible);
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
        else if (std::strcmp(interface, wl_seat_interface.name) == 0 && state.fSeatController.seat() == nullptr)
        {
            state.fSeatController.bindSeat(registry, name, version);
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

    void WaylandPlatformState::dispatchTasks()
    {
        std::vector<std::move_only_function<void()>> tasks;
        {
            const std::scoped_lock lock(fTaskMutex);
            tasks.swap(fTasks);
        }
        for (auto& task : tasks)
            task();
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
            result = wl_display_dispatch_pending(fDisplay);
        if (result < 0)
        {
            fQuitRequested = true;
            return;
        }

        const int flushResult = wl_display_flush(fDisplay);
        if (flushResult < 0 && errno != EAGAIN)
        {
            wl_display_cancel_read(fDisplay);
            fQuitRequested = true;
            return;
        }

        constexpr size_t DisplayDescriptor = 0;
        constexpr size_t WakeDescriptor = 1;
        constexpr size_t KeyRepeatDescriptor = 2;
        pollfd descriptors[]{
            {.fd = wl_display_get_fd(fDisplay),
             .events = static_cast<short>(POLLIN | (flushResult < 0 ? POLLOUT : 0)),
             .revents = 0},
            {.fd = fWakeDescriptor, .events = POLLIN, .revents = 0},
            {.fd = fSeatController.pollDescriptor(), .events = POLLIN, .revents = 0},
        };
        const int pollResult = poll(descriptors, std::size(descriptors), timeoutMilliseconds);
        if (pollResult > 0 && (descriptors[DisplayDescriptor].revents & POLLIN) != 0)
        {
            if (wl_display_read_events(fDisplay) < 0)
                fQuitRequested = true;
        }
        else
        {
            wl_display_cancel_read(fDisplay);
        }
        if (pollResult > 0 && (descriptors[DisplayDescriptor].revents & POLLOUT) != 0 &&
            wl_display_flush(fDisplay) < 0 && errno != EAGAIN)
        {
            fQuitRequested = true;
        }

        if (pollResult > 0 && (descriptors[WakeDescriptor].revents & POLLIN) != 0)
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

        if (pollResult > 0 && (descriptors[KeyRepeatDescriptor].revents & POLLIN) != 0)
            fSeatController.dispatchKeyRepeats();

        if (!fQuitRequested && wl_display_dispatch_pending(fDisplay) < 0)
            fQuitRequested = true;
    }

    void WaylandPlatformState::releaseObjects()
    {
        fSeatController.reset();
        fOutputManager.reset();
        fWindows.clear();
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
        fHasHostWindowFrame = false;
        {
            const std::scoped_lock lock(fTaskMutex);
            fTasks.clear();
        }
    }
}  // namespace LWS::internal

#endif
