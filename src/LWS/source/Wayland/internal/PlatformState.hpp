#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Platform.hpp>

    #include "../../internal/PlatformBackend.hpp"

    #include "WaylandDragAndDropController.hpp"
    #include "WaylandOutputManager.hpp"
    #include "WaylandSeatController.hpp"
    #include "WindowFrame.hpp"

    #include <cstdint>
    #include <unordered_map>
    #include <vector>

    #include <wayland-client.h>
    #include <xdg-decoration-client-protocol.h>
    #include <xdg-shell-client-protocol.h>
    #include <pointer-constraints-client-protocol.h>
    #include <relative-pointer-client-protocol.h>

namespace LWS
{
    class WindowBackendWayland;
}

namespace LWS::internal
{
    struct WaylandWindowRegistration
    {
        WindowBackendWayland* window = nullptr;
        WaylandSurfaceRole role = WaylandSurfaceRole::Content;
    };

    class WaylandPlatformState final : public PlatformBackend
    {
      public:

        WaylandPlatformState();

        Result Initialize() override;
        void Shutdown() override;
        [[nodiscard]] bool isInitialized() const;
        void RunMessageLoop(PlatformContext& context) override;
        [[nodiscard]] bool ProcessMessages(PlatformContext& context) override;
        [[nodiscard]] Result Wake() override;
        void ClearWake() override;
        [[nodiscard]] bool Supports(PlatformFeature feature) const override;
        [[nodiscard]] bool IsKeyPressed(KeyCode key) const override;
        [[nodiscard]] bool IsKeyToggled(KeyCode key) const override;
        [[nodiscard]] Point GetMousePosition() const override;
        [[nodiscard]] Result MoveMouse(Point delta) override;
        [[nodiscard]] Result RefreshMonitors() override;
        [[nodiscard]] MonitorDesc GetMonitorInfo(uintptr_t handle, bool allowRefresh) override;
        [[nodiscard]] MonitorDesc GetPrimaryMonitor(bool allowRefresh) override;
        [[nodiscard]] Rect GetBoundingMonitorArea() const override;
        [[nodiscard]] std::unique_ptr<IWindowBackend> CreateWindowBackend(Window& owner) override;

        void registerWindow(wl_surface* surface, WindowBackendWayland& window,
                            WaylandSurfaceRole role = WaylandSurfaceRole::Content);
        void unregisterWindow(wl_surface* surface);
        [[nodiscard]] const WaylandWindowRegistration* findWindowRegistration(wl_surface* surface) const;
        [[nodiscard]] WindowBackendWayland* findWindow(wl_surface* surface) const;
        void applyCursor(WindowBackendWayland& window, CursorShape shape, bool visible);

        [[nodiscard]] wl_display* display() const { return fDisplay; }
        [[nodiscard]] wl_compositor* compositor() const { return fCompositor; }
        [[nodiscard]] wl_subcompositor* subcompositor() const { return fSubcompositor; }
        [[nodiscard]] wl_shm* sharedMemory() const { return fSharedMemory; }
        [[nodiscard]] xdg_wm_base* shell() const { return fShell; }
        [[nodiscard]] zxdg_decoration_manager_v1* decorationManager() const { return fDecorationManager; }
        [[nodiscard]] zwp_pointer_constraints_v1* pointerConstraints() const { return fPointerConstraints; }
        [[nodiscard]] zwp_relative_pointer_manager_v1* relativePointerManager() const
        {
            return fRelativePointerManager;
        }
        [[nodiscard]] bool hasHostWindowFrame() const { return fHasHostWindowFrame; }
        [[nodiscard]] wl_pointer* pointer() const { return fSeatController.pointer(); }
        [[nodiscard]] wl_seat* seat() const { return fSeatController.seat(); }
        [[nodiscard]] uint32_t pointerButtonSerial() const { return fSeatController.pointerButtonSerial(); }
        [[nodiscard]] bool supportsDragAndDrop() const { return fDragAndDropController.supported(); }

      private:

        static void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                                   uint32_t version);
        static void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
        static void shellPing(void* data, xdg_wm_base* shell, uint32_t serial);

        void releaseObjects();
        void selectSeat(uint32_t name, uint32_t version);
        void dispatchOnce(PlatformContext& context, int timeoutMilliseconds);

        bool fInitialized = false;
        bool fHasHostWindowFrame = false;
        wl_display* fDisplay = nullptr;
        wl_registry* fRegistry = nullptr;
        wl_compositor* fCompositor = nullptr;
        wl_subcompositor* fSubcompositor = nullptr;
        wl_shm* fSharedMemory = nullptr;
        xdg_wm_base* fShell = nullptr;
        zxdg_decoration_manager_v1* fDecorationManager = nullptr;
        zwp_pointer_constraints_v1* fPointerConstraints = nullptr;
        zwp_relative_pointer_manager_v1* fRelativePointerManager = nullptr;
        int fWakeDescriptor = -1;
        WaylandSeatController fSeatController;
        WaylandDragAndDropController fDragAndDropController;
        WaylandOutputManager fOutputManager;
        std::unordered_map<wl_surface*, WaylandWindowRegistration> fWindows;
        struct SeatGlobal
        {
            uint32_t name;
            uint32_t version;
        };
        std::vector<SeatGlobal> fSeatGlobals;
        uint32_t fSelectedSeatName{};
        bool fAdditionalSeatReported{};
    };
}  // namespace LWS::internal

#endif
