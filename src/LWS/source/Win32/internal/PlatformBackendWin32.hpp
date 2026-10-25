#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <LWS/source/Win32/internal/WindowBackendWin32.hpp>
#include <LWS/source/internal/PlatformBackend.hpp>
#include "MonitorInfo.hpp"

namespace LWS::internal::platform_backend
{
    Result init();
    void shutdown();
    [[nodiscard]] bool supports(PlatformFeature feature);
    [[nodiscard]] bool isKeyPressed(KeyCode key);
    [[nodiscard]] bool isKeyToggled(KeyCode key);
    [[nodiscard]] Point getMousePosition();
    void moveMouse(Point delta);
    void refreshMonitors(MonitorInfo& monitors);
    [[nodiscard]] LWS::MonitorDesc getMonitorInfo(MonitorInfo& monitors, uintptr_t monitorHandle, bool allowRefresh);
    [[nodiscard]] LWS::MonitorDesc getPrimaryMonitor(MonitorInfo& monitors, bool allowRefresh);
    [[nodiscard]] Rect getBoundingMonitorArea(MonitorInfo& monitors);
}  // namespace LWS::internal::platform_backend

namespace LWS::internal
{
    // Windowing and capability behavior is identical for application-owned and host-owned message loops.
    // The selected concrete backend supplies task wakeups and dispatch without a runtime mode branch.
    class Win32PlatformBackendCommon : public PlatformBackend
    {
      public:

        bool Supports(PlatformFeature feature) const override { return platform_backend::supports(feature); }
        bool IsKeyPressed(KeyCode key) const override { return platform_backend::isKeyPressed(key); }
        bool IsKeyToggled(KeyCode key) const override { return platform_backend::isKeyToggled(key); }
        Point GetMousePosition() const override { return platform_backend::getMousePosition(); }
        Result MoveMouse(Point delta) override
        {
            platform_backend::moveMouse(delta);
            return Result::Success;
        }
        Result RefreshMonitors() override
        {
            platform_backend::refreshMonitors(monitors_);
            return Result::Success;
        }
        LWS::MonitorDesc GetMonitorInfo(uintptr_t handle, bool allowRefresh) override
        {
            return platform_backend::getMonitorInfo(monitors_, handle, allowRefresh);
        }
        LWS::MonitorDesc GetPrimaryMonitor(bool allowRefresh) override
        {
            return platform_backend::getPrimaryMonitor(monitors_, allowRefresh);
        }
        Rect GetBoundingMonitorArea() const override
        {
            return platform_backend::getBoundingMonitorArea(const_cast<MonitorInfo&>(monitors_));
        }
        std::unique_ptr<IWindowBackend> CreateWindowBackend(Window& owner) override
        {
            return std::make_unique<WindowBackendWin32>(owner, monitors_);
        }

      private:

        MonitorInfo monitors_;
    };
}  // namespace LWS::internal
