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

        Result init();
        void shutdown();
        [[nodiscard]] bool isInitialized();

        class [[nodiscard]] Session
        {
          public:

            Session();
            ~Session();

            Session(const Session&) = delete;
            Session& operator=(const Session&) = delete;
            Session(Session&&) = delete;
            Session& operator=(Session&&) = delete;

            [[nodiscard]] explicit operator bool() const { return fResult == Result::Success; }
            [[nodiscard]] Result GetResult() const { return fResult; }

          private:

            Result fResult;
        };

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
