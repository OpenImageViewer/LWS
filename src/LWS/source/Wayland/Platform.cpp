#ifdef LWS_PLATFORM_WAYLAND
    #include <LWS/Platform.hpp>
    #include <LWS/Wayland/PlatformWayland.hpp>
    #include "internal/PlatformState.hpp"

    #include <utility>

namespace LWS::Wayland
{
    wl_display* GetDisplay()
    {
        return internal::WaylandPlatformState::current().display();
    }

    void PostTask(std::move_only_function<void()> task)
    {
        internal::WaylandPlatformState::current().postTask(std::move(task));
    }
}  // namespace LWS::Wayland

namespace LWS::internal::platform_backend
{
    using Platform::Feature;
    using Platform::MonitorDesc;
    Result init()
    {
        return WaylandPlatformState::current().initialize();
    }
    void shutdown()
    {
        WaylandPlatformState::current().shutdown();
    }
    bool isInitialized()
    {
        return WaylandPlatformState::current().isInitialized();
    }
    void runMessageLoop()
    {
        WaylandPlatformState::current().runMessageLoop();
    }
    bool processMessages()
    {
        return WaylandPlatformState::current().processMessages();
    }
    void requestQuit()
    {
        WaylandPlatformState::current().requestQuit();
    }
    bool supports(Feature feature)
    {
        if (feature == Feature::ServerSideDecorations)
            return WaylandPlatformState::current().decorationManager() != nullptr;
        if (feature == Feature::PointerLock)
            return WaylandPlatformState::current().pointerConstraints() != nullptr &&
                   WaylandPlatformState::current().relativePointerManager() != nullptr;
        if (feature == Feature::DragAndDrop)
            return WaylandPlatformState::current().supportsDragAndDrop();
        return feature == Feature::HostWindowFrame && WaylandPlatformState::current().hasHostWindowFrame();
    }
    bool isKeyPressed(KeyCode key)
    {
        return WaylandPlatformState::current().isKeyPressed(key);
    }
    bool isKeyToggled(KeyCode)
    {
        return false;
    }
    Point getMousePosition()
    {
        return WaylandPlatformState::current().pointerPosition();
    }
    void moveMouse(Point) {}
    void browseToFile(const std::filesystem::path&) {}
    void refreshMonitors()
    {
        if (auto* display = WaylandPlatformState::current().display(); display != nullptr)
        {
            std::ignore = wl_display_roundtrip(display);
        }
    }
    MonitorDesc getMonitorInfo(Handle handle, bool allowRefresh)
    {
        if (allowRefresh)
            refreshMonitors();
        return WaylandPlatformState::current().monitorInfo(handle);
    }
    MonitorDesc getPrimaryMonitor(bool allowRefresh)
    {
        if (allowRefresh)
            refreshMonitors();
        return WaylandPlatformState::current().primaryMonitor();
    }
    Rect getBoundingMonitorArea()
    {
        return WaylandPlatformState::current().boundingMonitorArea();
    }
}  // namespace LWS::internal::platform_backend
#endif
