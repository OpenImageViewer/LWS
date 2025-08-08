#pragma once

#include <Windows.h>
#include <ShellScalingApi.h>

#include <cstdint>
#include <map>
#include <vector>

namespace LWS::internal
{
    struct MonitorDesc
    {
        DISPLAY_DEVICE displayInfo{};
        DEVMODE displaySettings{};
        MONITORINFOEX monitorInfo{};
        HMONITOR handle = nullptr;
        uint16_t dpiX = 0;
        uint16_t dpiY = 0;
    };

    class MonitorInfo
    {
    public:
        static MonitorInfo& instance();

        MonitorInfo();

        void refresh();
        const MonitorDesc& getMonitorInfo(HMONITOR hMonitor, bool allowRefresh = false);
        const MonitorDesc& getMonitorInfo(size_t index, bool allowRefresh = false);
        const MonitorDesc& getPrimaryMonitor(bool allowRefresh = false);
        size_t getMonitorsCount() const;
        RECT getBoundingMonitorArea();

    private:
        using MonitorMap = std::map<HMONITOR, MonitorDesc>;

        RECT getBoundingMonitorAreaInternal();
        static BOOL CALLBACK monitorEnumProc(HMONITOR hMonitor, HDC hdcMonitor, LPRECT lprcMonitor, LPARAM data);

        std::vector<MonitorDesc> mDisplayDevices;
        MonitorMap mHMonitorToDesc;
        MonitorMap::iterator fPrimaryMonitorIt = mHMonitorToDesc.end();
        inline static MonitorDesc mEmptyDesc{};
        bool fBoundAreaDirty = true;
        RECT fBoundArea{};
    };
}
