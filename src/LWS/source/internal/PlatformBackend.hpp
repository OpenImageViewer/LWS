#pragma once

#include <LWS/Platform.hpp>

#include <memory>

namespace LWS
{
    class Window;
}

namespace LWS::internal
{
    class IWindowBackend;

    class PlatformBackend
    {
      public:

        virtual ~PlatformBackend() = default;

        [[nodiscard]] virtual Result Initialize() = 0;
        virtual void Shutdown() = 0;
        virtual void RunMessageLoop(PlatformContext& context) = 0;
        [[nodiscard]] virtual bool ProcessMessages(PlatformContext& context) = 0;
        [[nodiscard]] virtual Result Wake() = 0;
        virtual void ClearWake() = 0;

        [[nodiscard]] virtual bool Supports(PlatformFeature feature) const = 0;
        [[nodiscard]] virtual bool IsKeyPressed(KeyCode key) const = 0;
        [[nodiscard]] virtual bool IsKeyToggled(KeyCode key) const = 0;
        [[nodiscard]] virtual Point GetMousePosition() const = 0;
        [[nodiscard]] virtual Result MoveMouse(Point delta) = 0;
        [[nodiscard]] virtual Result RefreshMonitors() = 0;
        [[nodiscard]] virtual MonitorDesc GetMonitorInfo(uintptr_t monitorHandle, bool allowRefresh) = 0;
        [[nodiscard]] virtual MonitorDesc GetPrimaryMonitor(bool allowRefresh) = 0;
        [[nodiscard]] virtual Rect GetBoundingMonitorArea() const = 0;
        [[nodiscard]] virtual std::unique_ptr<IWindowBackend> CreateWindowBackend(Window& owner) = 0;
    };

    [[nodiscard]] std::unique_ptr<PlatformBackend> CreatePlatformBackend(BackendId backend);

    class PlatformContextAccess final
    {
      public:

        static void DrainTasks(PlatformContext& context);
        [[nodiscard]] static bool QuitRequested(const PlatformContext& context);
        [[nodiscard]] static std::unique_ptr<IWindowBackend> CreateWindowBackend(PlatformContext& context,
                                                                                 Window& owner);
    };
}  // namespace LWS::internal
