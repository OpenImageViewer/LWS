#ifdef LWS_PLATFORM_X11
#include <LWS/Platform.hpp>

namespace LWS::Platform
{
    namespace
    {
        thread_local uint32_t g_initCount = 0;
    }

    Result init()
    {
        ++g_initCount;
        return Result::Success;
    }
    void shutdown()
    {
        if (g_initCount != 0)
            --g_initCount;
    }
    bool isInitialized()
    {
        return g_initCount != 0;
    }
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
