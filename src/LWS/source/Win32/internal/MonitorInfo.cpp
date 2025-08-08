#include "MonitorInfo.hpp"

#include <algorithm>
#include <ranges>

namespace LWS::internal
{
    MonitorInfo& MonitorInfo::instance()
    {
        static MonitorInfo s_instance;
        return s_instance;
    }

    MonitorInfo::MonitorInfo()
    {
        refresh();
    }

    void MonitorInfo::refresh()
    {
        fBoundAreaDirty = true;
        mDisplayDevices.clear();
        mHMonitorToDesc.clear();
        fPrimaryMonitorIt = mHMonitorToDesc.end();

        for (DWORD device_index = 0; ; ++device_index)
        {
            DISPLAY_DEVICE display_device{};
            display_device.cb = sizeof(display_device);
            if (EnumDisplayDevices(nullptr, device_index, &display_device, 0) == FALSE)
            {
                break;
            }

            if ((display_device.StateFlags & DISPLAY_DEVICE_ACTIVE) != DISPLAY_DEVICE_ACTIVE)
            {
                continue;
            }

            MonitorDesc desc{};
            desc.displayInfo = display_device;
            desc.displaySettings.dmSize = static_cast<WORD>(sizeof(DEVMODE));
            EnumDisplaySettings(display_device.DeviceName, ENUM_CURRENT_SETTINGS, &desc.displaySettings);
            mDisplayDevices.push_back(desc);
        }

        EnumDisplayMonitors(nullptr, nullptr, monitorEnumProc, reinterpret_cast<LPARAM>(this));

        if (fPrimaryMonitorIt == mHMonitorToDesc.end() && mHMonitorToDesc.empty() == false)
        {
            fPrimaryMonitorIt = mHMonitorToDesc.begin();
        }
    }

    const MonitorDesc& MonitorInfo::getMonitorInfo(HMONITOR hMonitor, bool allowRefresh)
    {
        if (allowRefresh)
        {
            refresh();
        }

        MonitorMap::const_iterator it = mHMonitorToDesc.find(hMonitor);
        return it == mHMonitorToDesc.end() ? mEmptyDesc : it->second;
    }

    const MonitorDesc& MonitorInfo::getMonitorInfo(size_t index, bool allowRefresh)
    {
        if (index >= mDisplayDevices.size() && allowRefresh)
        {
            refresh();
        }

        return index < mDisplayDevices.size() ? mDisplayDevices[index] : mEmptyDesc;
    }

    const MonitorDesc& MonitorInfo::getPrimaryMonitor(bool allowRefresh)
    {
        if (allowRefresh)
        {
            refresh();
        }

        return fPrimaryMonitorIt == mHMonitorToDesc.end() ? mEmptyDesc : fPrimaryMonitorIt->second;
    }

    size_t MonitorInfo::getMonitorsCount() const
    {
        return mDisplayDevices.size();
    }

    RECT MonitorInfo::getBoundingMonitorArea()
    {
        if (fBoundAreaDirty)
        {
            fBoundArea = getBoundingMonitorAreaInternal();
            fBoundAreaDirty = false;
        }

        return fBoundArea;
    }

    RECT MonitorInfo::getBoundingMonitorAreaInternal()
    {
        if (mDisplayDevices.empty())
        {
            return {};
        }

        return std::ranges::fold_left(
            mDisplayDevices | std::views::drop(1U),
            mDisplayDevices.front().monitorInfo.rcMonitor,
            [](RECT acc, const MonitorDesc& d) -> RECT
            {
                const RECT& r = d.monitorInfo.rcMonitor;
                return {
                    (std::min)(acc.left, r.left),
                    (std::min)(acc.top, r.top),
                    (std::max)(acc.right, r.right),
                    (std::max)(acc.bottom, r.bottom)
                };
            });
    }

    BOOL CALLBACK MonitorInfo::monitorEnumProc(HMONITOR hMonitor, HDC, LPRECT, LPARAM data)
    {
        MonitorInfo* self = reinterpret_cast<MonitorInfo*>(data);
        MONITORINFOEX monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfo(hMonitor, &monitor_info) == FALSE)
        {
            return TRUE;
        }

        for (MonitorDesc& desc : self->mDisplayDevices)
        {
            if (lstrcmpi(monitor_info.szDevice, desc.displayInfo.DeviceName) != 0)
            {
                continue;
            }

            desc.monitorInfo = monitor_info;
            desc.handle = hMonitor;

            UINT dpi_x = 96;
            UINT dpi_y = 96;
            if (FAILED(GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y)))
            {
                HWND desktop_window = GetDesktopWindow();
                HDC desktop_dc = GetDC(desktop_window);
                if (desktop_dc != nullptr)
                {
                    dpi_x = static_cast<UINT>(GetDeviceCaps(desktop_dc, LOGPIXELSX));
                    dpi_y = static_cast<UINT>(GetDeviceCaps(desktop_dc, LOGPIXELSY));
                    ReleaseDC(desktop_window, desktop_dc);
                }
            }

            desc.dpiX = static_cast<uint16_t>(dpi_x);
            desc.dpiY = static_cast<uint16_t>(dpi_y);

            auto [it, _] = self->mHMonitorToDesc.insert({ hMonitor, desc });
            if ((monitor_info.dwFlags & MONITORINFOF_PRIMARY) == MONITORINFOF_PRIMARY)
            {
                self->fPrimaryMonitorIt = it;
            }

            break;
        }

        return TRUE;
    }
}
