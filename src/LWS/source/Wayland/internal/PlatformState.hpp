#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Platform.hpp>

    #include "WaylandCursorController.hpp"
    #include "WaylandOutputManager.hpp"
    #include "WindowFrame.hpp"

    #include <cstdint>
    #include <functional>
    #include <mutex>
    #include <unordered_map>
    #include <unordered_set>
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
    class WaylandPlatformState
    {
      public:

        static WaylandPlatformState& current();

        Result initialize();
        void shutdown();
        [[nodiscard]] bool isInitialized() const;

        void runMessageLoop();
        [[nodiscard]] bool processMessages();
        void requestQuit();
        void postTask(std::move_only_function<void()> task);

        void registerWindow(wl_surface* surface, WindowBackendWayland& window,
                            WaylandSurfaceRole role = WaylandSurfaceRole::Content);
        void unregisterWindow(wl_surface* surface);
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
        [[nodiscard]] wl_pointer* pointer() const { return fPointer; }
        [[nodiscard]] wl_seat* seat() const { return fSeat; }
        [[nodiscard]] uint32_t pointerButtonSerial() const { return fPointerButtonSerial; }
        [[nodiscard]] Point pointerPosition() const { return fPointerPosition; }
        [[nodiscard]] bool isKeyPressed(KeyCode key) const;
        [[nodiscard]] Platform::MonitorDesc monitorInfo(Handle handle) const
        {
            return fOutputManager.monitorInfo(handle);
        }
        [[nodiscard]] Platform::MonitorDesc primaryMonitor() const { return fOutputManager.primaryMonitor(); }
        [[nodiscard]] Rect boundingMonitorArea() const { return fOutputManager.boundingMonitorArea(); }

      private:

        struct WindowRegistration
        {
            WindowBackendWayland* window = nullptr;
            WaylandSurfaceRole role = WaylandSurfaceRole::Content;
        };

        static void registryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface,
                                   uint32_t version);
        static void registryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
        static void shellPing(void* data, xdg_wm_base* shell, uint32_t serial);
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
        void dispatchKeyRepeats();
        void releaseObjects();
        void startKeyRepeat(KeyCode key);
        void stopKeyRepeat();
        void dispatchTasks();
        void dispatchOnce(int timeoutMilliseconds);

        uint32_t fInitCount = 0;
        bool fQuitRequested = false;
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
        wl_seat* fSeat = nullptr;
        wl_pointer* fPointer = nullptr;
        wl_keyboard* fKeyboard = nullptr;
        int fWakeDescriptor = -1;
        int fKeyRepeatDescriptor = -1;
        int32_t fKeyRepeatRate = 0;
        int32_t fKeyRepeatDelay = 0;
        KeyCode fRepeatingKey = KeyCode::Unknown;
        std::mutex fTaskMutex;
        std::vector<std::move_only_function<void()>> fTasks;
        WaylandCursorController fCursorController;
        WindowBackendWayland* fPointerWindow = nullptr;
        WaylandSurfaceRole fPointerSurfaceRole = WaylandSurfaceRole::Content;
        WindowBackendWayland* fKeyboardWindow = nullptr;
        uint32_t fPointerButtonSerial = 0;
        uint32_t fPointerEnterSerial = 0;
        Point fPointerPosition{};
        std::unordered_set<KeyCode> fPressedKeys;
        std::unordered_map<wl_surface*, WindowRegistration> fWindows;
        WaylandOutputManager fOutputManager;
    };
}  // namespace LWS::internal

#endif
