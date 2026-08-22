#pragma once
#include <LWS/interfaces/backends.hpp>
#include <filesystem>

namespace LWS
{
    namespace Platform
    {
        struct MonitorDesc
        {
            Handle handle = 0;
            string_type deviceName;
            Rect monitorRect;
            Rect workRect;
            uint32_t dpiX = 96;
            uint32_t dpiY = 96;
            uint32_t displayFrequency = 60;
            bool primary = false;
        };

        void init();
        void shutdown();
        void runMessageLoop();
        bool processMessages();

        bool isKeyPressed(KeyCode key);
        bool isKeyToggled(KeyCode key);
        Point getMousePosition();
        void moveMouse(Point delta);

        void browseToFile(const std::filesystem::path& path);

        void refreshMonitors();
        MonitorDesc getMonitorInfo(Handle monitorHandle, bool allowRefresh = false);
        MonitorDesc getPrimaryMonitor(bool allowRefresh = false);
        Rect getBoundingMonitorArea();
    }
}
