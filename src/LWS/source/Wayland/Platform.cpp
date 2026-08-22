#ifdef LWS_PLATFORM_WAYLAND
#include <LWS/Platform.hpp>

namespace LWS::Platform
{
    void init() {}
    void shutdown() {}
    void runMessageLoop() {}
    bool processMessages() { return false; }
    bool isKeyPressed(KeyCode) { return false; }
    bool isKeyToggled(KeyCode) { return false; }
    Point getMousePosition() { return {}; }
    void moveMouse(Point) {}
    void browseToFile(const std::filesystem::path&) {}
    void refreshMonitors() {}
    MonitorDesc getMonitorInfo(Handle, bool) { return {}; }
    MonitorDesc getPrimaryMonitor(bool) { return {}; }
    Rect getBoundingMonitorArea() { return {}; }
}
#endif
