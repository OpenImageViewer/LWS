#pragma once
#include <LWS/source/internal/PlatformBackend.hpp>
#include "MonitorInfo.hpp"
namespace LWS::internal::platform_backend
{
    Result init();
    void shutdown();
}  // namespace LWS::internal::platform_backend
namespace LWS::internal
{
    // Both dispatchers share windowing and capabilities while retaining their distinct wakeup contracts.
    class Win32PlatformBackendCommon : public PlatformBackend
    {
      public:

        bool Supports(PlatformFeature feature) const override;
        bool IsKeyPressed(KeyCode key) const override;
        bool IsKeyToggled(KeyCode key) const override;
        Point GetMousePosition() const override;
        Result MoveMouse(Point delta) override;
        Result RefreshMonitors() override;
        LWS::MonitorDesc GetMonitorInfo(uintptr_t handle, bool allowRefresh) override;
        LWS::MonitorDesc GetPrimaryMonitor(bool allowRefresh) override;
        Rect GetBoundingMonitorArea() const override;
        std::unique_ptr<IWindowBackend> CreateWindowBackend(Window& owner) override;

      private:

        mutable MonitorInfo monitors_;
    };
}  // namespace LWS::internal
