#ifdef LWS_PLATFORM_WIN32

    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <LWS/source/Win32/internal/WindowBackendWin32.hpp>

    #include "../internal/PlatformBackend.hpp"
    #include "internal/MonitorInfo.hpp"

    #include <tuple>

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
    class Win32PlatformBackend final : public PlatformBackend
    {
      public:

        ~Win32PlatformBackend() override { Shutdown(); }

        Result Initialize() override
        {
            const Result result = platform_backend::init();
            if (result != Result::Success)
                return result;
            taskEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (taskEvent_ == nullptr)
            {
                platform_backend::shutdown();
                return Result::Failure;
            }
            return Result::Success;
        }

        void Shutdown() override
        {
            if (taskEvent_ != nullptr)
            {
                CloseHandle(taskEvent_);
                taskEvent_ = nullptr;
                platform_backend::shutdown();
            }
        }

        void RunMessageLoop(PlatformContext& context) override
        {
            while (!PlatformContextAccess::QuitRequested(context))
            {
                const DWORD result = MsgWaitForMultipleObjectsEx(1, &taskEvent_, INFINITE, QS_ALLINPUT,
                                                                 MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0)
                    PlatformContextAccess::DrainTasks(context);
                if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1)
                    DispatchMessages(context);
                else if (result == WAIT_FAILED)
                    context.RequestQuit();
            }
        }

        bool ProcessMessages(PlatformContext& context) override
        {
            if (WaitForSingleObject(taskEvent_, 0) == WAIT_OBJECT_0)
                PlatformContextAccess::DrainTasks(context);
            DispatchMessages(context);
            return PlatformContextAccess::QuitRequested(context);
        }

        Result Wake() override { return SetEvent(taskEvent_) != FALSE ? Result::Success : Result::Failure; }
        void ClearWake() override { std::ignore = ResetEvent(taskEvent_); }

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

        static void DispatchMessages(PlatformContext& context)
        {
            MSG message{};
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
            {
                if (message.message == WM_QUIT)
                    context.RequestQuit();
                else
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }

        HANDLE taskEvent_{};
        MonitorInfo monitors_;
    };

    std::unique_ptr<PlatformBackend> CreatePlatformBackend(BackendId backend)
    {
        if (backend == BackendId::Win32)
            return std::make_unique<Win32PlatformBackend>();
        return nullptr;
    }
}  // namespace LWS::internal

#endif
