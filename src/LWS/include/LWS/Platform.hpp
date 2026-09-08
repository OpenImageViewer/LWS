#pragma once

#include <LWS/KeyCode.hpp>
#include <LWS/Result.hpp>
#include <LWS/WindowTypes.hpp>

#include <exception>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace LWS::internal
{
    class ListenerState;
    class PlatformContextAccess;
}  // namespace LWS::internal

namespace LWS
{
    enum class PlatformFeature
    {
        AbsoluteWindowPosition,
        GlobalPointerPosition,
        PointerWarp,
        ProgrammaticActivation,
        AlwaysOnTop,
        MultiMonitorFullscreen,
        PointerLock,
        WindowIcon,
        Clipboard,
        DragAndDrop,
        FileDialog,
        NotificationIcon,
        NotificationIconGeometry,
        ServerSideDecorations,
        HostWindowFrame
    };

    struct MonitorDesc
    {
        uintptr_t handle{};
        string_type deviceName;
        Rect monitorRect;
        Rect workRect;
        ContentScale contentScale;
        Size pixelSize;
        uint32_t displayFrequency{60};
        bool primary{};
    };

    struct PlatformConfig
    {
        BackendId backend{BackendId::Undefined};
    };

    using UnhandledExceptionHandler = std::move_only_function<void(std::exception_ptr) noexcept>;

    /// Owns one UI-thread identity and at most one successful backend lifetime.
    ///
    /// Failed Init() attempts may retry. Shutdown() after a successful Init() is terminal; use another context for
    /// another backend, thread, or active lifetime. The context must outlive every bound window and service.
    ///
    /// @par Thread safety
    /// Instance operations require the construction thread unless stated otherwise. Debug builds assert affinity and
    /// wrong-thread release use is undefined. PostTask(), RequestQuit(), and IsCurrentThread() are thread-safe;
    /// GetAvailableBackends() is thread-neutral. All callbacks execute synchronously on the context thread.
    class PlatformContext final
    {
      public:

        PlatformContext();
        ~PlatformContext();

        PlatformContext(const PlatformContext&) = delete;
        PlatformContext& operator=(const PlatformContext&) = delete;
        PlatformContext(PlatformContext&&) = delete;
        PlatformContext& operator=(PlatformContext&&) = delete;

        [[nodiscard]] Result Init(const PlatformConfig& config);
        /// Rejects shutdown during message dispatch or while windows or services remain bound.
        /// Accepted tasks are drained before shutdown; resources they create keep the context active.
        [[nodiscard]] Result Shutdown();
        [[nodiscard]] bool IsUsable() const;
        [[nodiscard]] bool IsCurrentThread() const noexcept;
        [[nodiscard]] std::optional<BackendId> GetBackendId() const;
        [[nodiscard]] static std::vector<BackendId> GetAvailableBackends();

        void RunMessageLoop();
        [[nodiscard]] bool ProcessMessages();
        [[nodiscard]] Result PostTask(std::move_only_function<void()> task);
        void RequestQuit();

        void SetUnhandledExceptionHandler(UnhandledExceptionHandler handler);

        [[nodiscard]] std::expected<bool, Result> Supports(PlatformFeature feature) const;
        [[nodiscard]] std::expected<bool, Result> IsKeyPressed(KeyCode key) const;
        [[nodiscard]] std::expected<bool, Result> IsKeyToggled(KeyCode key) const;
        [[nodiscard]] std::expected<Point, Result> GetMousePosition() const;
        [[nodiscard]] Result MoveMouse(Point delta);
        [[nodiscard]] Result RefreshMonitors();
        [[nodiscard]] std::expected<MonitorDesc, Result> GetMonitorInfo(uintptr_t monitorHandle,
                                                                        bool allowRefresh = false);
        [[nodiscard]] std::expected<MonitorDesc, Result> GetPrimaryMonitor(bool allowRefresh = false);
        [[nodiscard]] std::expected<Rect, Result> GetBoundingMonitorArea() const;

        void AssertCurrentThread() const noexcept;

      private:

        friend class Window;
        friend class Timer;
        friend class HighPrecisionTimer;
        friend class NotificationIconGroup;
        friend class Clipboard;
        friend class internal::ListenerState;
        friend class internal::PlatformContextAccess;

        void InvokeUserTask(std::move_only_function<void()>& task) noexcept;
        void ReportUnhandledException(std::exception_ptr exception) noexcept;
        void RegisterWindow();
        void UnregisterWindow();
        void RegisterService();
        void UnregisterService();

        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
