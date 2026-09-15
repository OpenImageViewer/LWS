#ifdef LWS_PLATFORM_WAYLAND

    #include "PlatformState.hpp"

    #include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <cerrno>
    #include <cstring>
    #include <cstdio>
    #include <poll.h>
    #include <sys/eventfd.h>
    #include <tuple>
    #include <unistd.h>

namespace LWS::internal
{
    WaylandPlatformState::WaylandPlatformState(PlatformContext& context)
        : fContext(context), fSeatController(*this), fDragAndDropController(*this), fOutputManager(*this)
    {
    }

    Result WaylandPlatformState::Initialize()
    {
        if (fInitialized)
            return Result::InvalidState;

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

        fInitialized = true;
        return Result::Success;
    }

    void WaylandPlatformState::Shutdown()
    {
        if (fInitialized)
            releaseObjects();
    }

    bool WaylandPlatformState::isInitialized() const
    {
        return fInitialized;
    }

    LoopResult WaylandPlatformState::RunMessageLoop(PlatformContext& context)
    {
        while (!PlatformContextAccess::QuitRequested(context) && fDisplay != nullptr)
            dispatchOnce(context, -1);
        return PlatformContextAccess::GetLoopResult(context);
    }

    LoopResult WaylandPlatformState::ProcessMessages(PlatformContext& context)
    {
        dispatchOnce(context, 0);
        return PlatformContextAccess::GetLoopResult(context);
    }

    Result WaylandPlatformState::Wake()
    {
        if (fWakeDescriptor < 0)
            return Result::InvalidState;
        const uint64_t value = 1;
        ssize_t written;
        do
        {
            written = write(fWakeDescriptor, &value, sizeof(value));
        } while (written < 0 && errno == EINTR);
        return written == sizeof(value) || (written < 0 && errno == EAGAIN) ? Result::Success : Result::Failure;
    }

    void WaylandPlatformState::ClearWake()
    {
        if (fWakeDescriptor >= 0)
        {
            uint64_t value{};
            while (read(fWakeDescriptor, &value, sizeof(value)) > 0)
            {
            }
        }
    }

    bool WaylandPlatformState::Supports(PlatformFeature feature) const
    {
        if (feature == PlatformFeature::ServerSideDecorations)
            return fDecorationManager != nullptr;
        if (feature == PlatformFeature::PointerLock)
            return fPointerConstraints != nullptr && fRelativePointerManager != nullptr;
        if (feature == PlatformFeature::DragAndDrop)
            return supportsDragAndDrop();
        return feature == PlatformFeature::HostWindowFrame && fHasHostWindowFrame;
    }

    bool WaylandPlatformState::IsKeyPressed(KeyCode key) const
    {
        return fSeatController.isKeyPressed(key);
    }
    bool WaylandPlatformState::IsKeyToggled(KeyCode) const
    {
        return false;
    }
    Point WaylandPlatformState::GetMousePosition() const
    {
        return fSeatController.pointerPosition();
    }
    Result WaylandPlatformState::MoveMouse(Point)
    {
        return Result::NotSupported;
    }

    Result WaylandPlatformState::RefreshMonitors()
    {
        if (wl_display_roundtrip(fDisplay) < 0)
            PlatformContextAccess::Fail(fContext, wl_display_get_error(fDisplay), "wl_display_roundtrip");
        return fContext.IsUsable() ? Result::Success : Result::Failure;
    }

    MonitorDesc WaylandPlatformState::GetMonitorInfo(uintptr_t handle, bool allowRefresh)
    {
        if (allowRefresh)
            std::ignore = RefreshMonitors();
        return fOutputManager.monitorInfo(handle);
    }

    MonitorDesc WaylandPlatformState::GetPrimaryMonitor(bool allowRefresh)
    {
        if (allowRefresh)
            std::ignore = RefreshMonitors();
        return fOutputManager.primaryMonitor();
    }

    Rect WaylandPlatformState::GetBoundingMonitorArea() const
    {
        return fOutputManager.boundingMonitorArea();
    }

    std::unique_ptr<IWindowBackend> WaylandPlatformState::CreateWindowBackend(Window& owner)
    {
        return std::make_unique<WindowBackendWayland>(owner, *this);
    }

    void WaylandPlatformState::registerWindow(wl_surface* surface, WindowBackendWayland& window,
                                              WaylandSurfaceRole role)
    {
        fWindows.emplace(surface, WaylandWindowRegistration{&window, role});
    }

    void WaylandPlatformState::unregisterWindow(wl_surface* surface)
    {
        if (WindowBackendWayland* window = findWindow(surface); window != nullptr)
        {
            fSeatController.windowRemoved(*window);
            fDragAndDropController.windowRemoved(*window);
        }
        fWindows.erase(surface);
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

    void WaylandPlatformState::outputChanged(wl_output* output, bool removed)
    {
        std::vector<wl_surface*> surfaces;
        for (const auto& [surface, registration] : fWindows)
        {
            if (registration.role == WaylandSurfaceRole::Content)
                surfaces.push_back(surface);
        }
        for (wl_surface* surface : surfaces)
        {
            if (WindowBackendWayland* window = findWindow(surface); window != nullptr)
                window->handleOutputChange(output, removed);
        }
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
        else if (std::strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0)
        {
            state.fFractionalScaleManager = static_cast<wp_fractional_scale_manager_v1*>(
                wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, 1));
        }
        else if (std::strcmp(interface, wp_viewporter_interface.name) == 0)
        {
            state.fViewporter = static_cast<wp_viewporter*>(
                wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
        }
        else if (std::strcmp(interface, wl_data_device_manager_interface.name) == 0)
        {
            state.fDragAndDropController.bindManager(registry, name, version);
        }
        else if (std::strcmp(interface, "weston_rdprail_shell") == 0)
        {
            state.fHasHostWindowFrame = true;
        }
        else if (std::strcmp(interface, wl_seat_interface.name) == 0)
        {
            state.fSeatGlobals.push_back({name, version});
            if (state.fSelectedSeatName == 0)
                state.selectSeat(name, version);
            else if (!state.fAdditionalSeatReported)
            {
                std::fputs("LWS: additional Wayland seats are ignored\n", stderr);
                state.fAdditionalSeatReported = true;
            }
        }
        else if (std::strcmp(interface, wl_output_interface.name) == 0)
        {
            state.fOutputManager.bindOutput(registry, name, version);
        }
    }

    void WaylandPlatformState::registryGlobalRemove(void* data, wl_registry*, uint32_t name)
    {
        auto& state = *static_cast<WaylandPlatformState*>(data);
        state.fOutputManager.removeGlobal(name);
        const auto seat = std::ranges::find(state.fSeatGlobals, name, &SeatGlobal::name);
        if (seat == state.fSeatGlobals.end())
            return;
        const bool selected = name == state.fSelectedSeatName;
        state.fSeatGlobals.erase(seat);
        if (selected)
        {
            state.fDragAndDropController.setSeat(nullptr);
            state.fSeatController.reset();
            state.fSelectedSeatName = 0;
            if (!state.fSeatGlobals.empty())
            {
                state.fSeatController.initialize();
                state.selectSeat(state.fSeatGlobals.front().name, state.fSeatGlobals.front().version);
            }
        }
    }

    void WaylandPlatformState::selectSeat(uint32_t name, uint32_t version)
    {
        fSeatController.bindSeat(fRegistry, name, version);
        fSelectedSeatName = name;
        fDragAndDropController.setSeat(fSeatController.seat());
    }

    void WaylandPlatformState::shellPing(void*, xdg_wm_base* shell, uint32_t serial)
    {
        xdg_wm_base_pong(shell, serial);
    }

    void WaylandPlatformState::dispatchOnce(PlatformContext& context, int timeoutMilliseconds)
    {
        if (!context.IsUsable())
            return;
        if (fDisplay == nullptr)
        {
            PlatformContextAccess::Fail(context, ENOTCONN, "Wayland display");
            return;
        }
        const auto fail = [&](const char* operation)
        {
            const int displayError = wl_display_get_error(fDisplay);
            PlatformContextAccess::Fail(context, displayError != 0 ? displayError : errno, operation);
        };

        int result = wl_display_dispatch_pending(fDisplay);
        while (context.IsUsable() && result >= 0 && wl_display_prepare_read(fDisplay) != 0)
            result = wl_display_dispatch_pending(fDisplay);
        if (!context.IsUsable())
            return;
        if (result < 0)
        {
            fail("wl_display_dispatch_pending");
            return;
        }

        const int flushResult = wl_display_flush(fDisplay);
        if (flushResult < 0 && errno != EAGAIN)
        {
            wl_display_cancel_read(fDisplay);
            fail("wl_display_flush");
            return;
        }

        constexpr size_t DisplayDescriptor = 0;
        constexpr size_t WakeDescriptor = 1;
        constexpr size_t KeyRepeatDescriptor = 2;
        constexpr size_t DropDescriptor = 3;
        pollfd descriptors[]{
            {.fd = wl_display_get_fd(fDisplay),
             .events = static_cast<short>(POLLIN | (flushResult < 0 ? POLLOUT : 0)),
             .revents = 0},
            {.fd = fWakeDescriptor, .events = POLLIN, .revents = 0},
            {.fd = fSeatController.pollDescriptor(), .events = POLLIN, .revents = 0},
            {.fd = fDragAndDropController.pollDescriptor(), .events = POLLIN, .revents = 0},
        };
        const int pollResult = poll(descriptors, std::size(descriptors), timeoutMilliseconds);
        const int pollError = errno;
        // Every successful prepare_read is paired before callbacks or early failure returns.
        if (pollResult > 0 && (descriptors[DisplayDescriptor].revents & POLLIN) != 0)
        {
            if (wl_display_read_events(fDisplay) < 0)
                fail("wl_display_read_events");
        }
        else
            wl_display_cancel_read(fDisplay);

        if (pollResult < 0 && pollError != EINTR)
            PlatformContextAccess::Fail(context, pollError, "poll");
        if (pollResult > 0 && (descriptors[DisplayDescriptor].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            PlatformContextAccess::Fail(context, wl_display_get_error(fDisplay), "Wayland display disconnected");
        if (pollResult > 0 && (descriptors[WakeDescriptor].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0)
            PlatformContextAccess::Fail(context, EBADF, "Wayland wake descriptor");
        if (!context.IsUsable())
            return;
        if (pollResult > 0 && (descriptors[DisplayDescriptor].revents & POLLOUT) != 0 &&
            wl_display_flush(fDisplay) < 0 && errno != EAGAIN)
        {
            fail("wl_display_flush");
            return;
        }
        if (pollResult > 0 && (descriptors[WakeDescriptor].revents & POLLIN) != 0)
            PlatformContextAccess::DrainTasks(context);
        if (context.IsUsable() && pollResult > 0 && (descriptors[KeyRepeatDescriptor].revents & POLLIN) != 0)
            fSeatController.dispatchKeyRepeats();
        if (context.IsUsable() && pollResult > 0)
            fDragAndDropController.processEvents(descriptors[DropDescriptor].revents);
        if (context.IsUsable() && !PlatformContextAccess::QuitRequested(context) &&
            wl_display_dispatch_pending(fDisplay) < 0)
            fail("wl_display_dispatch_pending");
    }

    void WaylandPlatformState::releaseObjects()
    {
        fDragAndDropController.reset();
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
        if (fFractionalScaleManager != nullptr)
            wp_fractional_scale_manager_v1_destroy(fFractionalScaleManager);
        if (fViewporter != nullptr)
            wp_viewporter_destroy(fViewporter);
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
        fFractionalScaleManager = nullptr;
        fViewporter = nullptr;
        fSharedMemory = nullptr;
        fSubcompositor = nullptr;
        fCompositor = nullptr;
        fRegistry = nullptr;
        fDisplay = nullptr;
        fWakeDescriptor = -1;
        fHasHostWindowFrame = false;
        fSeatGlobals.clear();
        fSelectedSeatName = 0;
        fAdditionalSeatReported = false;
        fInitialized = false;
    }
}  // namespace LWS::internal

#endif
