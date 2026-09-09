#include <LWS/Platform.hpp>

#include "internal/PlatformBackend.hpp"

#include <LWS/source/internal/Backends.hpp>

#include <cassert>
#include <mutex>

namespace LWS
{
    namespace
    {
        thread_local PlatformContext* activeContext;
    }

    class PlatformContext::Impl final
    {
      public:

        enum class State
        {
            Uninitialized,
            Active,
            Stopped
        };

        explicit Impl(std::thread::id threadId) : threadId(threadId) {}

        const std::thread::id threadId;
        State state{State::Uninitialized};
        std::optional<BackendId> backendId;
        std::unique_ptr<internal::PlatformBackend> backend;
        std::mutex taskMutex;
        std::vector<std::move_only_function<void()>> tasks;
        bool acceptingTasks{};
        bool wakePending{};
        bool quitRequested{};
        class DispatchScope final
        {
          public:

            explicit DispatchScope(size_t& depth) : depth_(depth) { ++depth_; }
            ~DispatchScope() { --depth_; }

          private:

            size_t& depth_;
        };

        size_t dispatchDepth{};
        size_t boundWindowCount{};
        size_t boundServiceCount{};
        std::shared_ptr<UnhandledExceptionHandler> exceptionHandler;
    };

    PlatformContext::PlatformContext() : impl_(std::make_unique<Impl>(std::this_thread::get_id())) {}

    PlatformContext::~PlatformContext()
    {
        AssertCurrentThread();
        assert(impl_->dispatchDepth == 0 && impl_->boundWindowCount == 0 && impl_->boundServiceCount == 0);
        std::ignore = Shutdown();
    }

    Result PlatformContext::Init(const PlatformConfig& config)
    {
        AssertCurrentThread();
        if (impl_->state != Impl::State::Uninitialized)
            return Result::InvalidState;
        if (activeContext != nullptr)
            return Result::InvalidState;
        if (config.backend == BackendId::Undefined)
            return Result::InvalidArgument;

        auto backend = internal::CreatePlatformBackend(config.backend);
        if (backend == nullptr)
            return Result::NotSupported;
        const Result result = backend->Initialize();
        if (result != Result::Success)
            return result;

        impl_->backendId = config.backend;
        {
            const std::scoped_lock lock(impl_->taskMutex);
            impl_->backend = std::move(backend);
            impl_->acceptingTasks = true;
            impl_->quitRequested = false;
        }
        impl_->state = Impl::State::Active;
        activeContext = this;
        return Result::Success;
    }

    Result PlatformContext::Shutdown()
    {
        AssertCurrentThread();
        if (impl_->state == Impl::State::Uninitialized || impl_->state == Impl::State::Stopped)
            return Result::Success;
        if (impl_->dispatchDepth != 0 || impl_->boundWindowCount != 0 || impl_->boundServiceCount != 0)
            return Result::InvalidState;

        {
            const std::scoped_lock lock(impl_->taskMutex);
            impl_->acceptingTasks = false;
        }
        {
            const Impl::DispatchScope dispatch(impl_->dispatchDepth);
            internal::PlatformContextAccess::DrainTasks(*this);
        }
        std::unique_ptr<internal::PlatformBackend> backend;
        {
            const std::scoped_lock lock(impl_->taskMutex);
            // Accepted tasks may have created a persistent window or service while draining.
            if (impl_->boundWindowCount != 0 || impl_->boundServiceCount != 0)
            {
                impl_->acceptingTasks = true;
                return Result::InvalidState;
            }
            backend = std::move(impl_->backend);
        }
        backend->Shutdown();
        impl_->state = Impl::State::Stopped;
        activeContext = nullptr;
        return Result::Success;
    }

    bool PlatformContext::IsUsable() const
    {
        AssertCurrentThread();
        return impl_->state == Impl::State::Active;
    }

    bool PlatformContext::IsCurrentThread() const noexcept
    {
        return impl_->threadId == std::this_thread::get_id();
    }

    std::optional<BackendId> PlatformContext::GetBackendId() const
    {
        AssertCurrentThread();
        return impl_->backendId;
    }

    std::vector<BackendId> PlatformContext::GetAvailableBackends()
    {
        std::vector<BackendId> backends;
#ifdef LWS_HAS_WIN32_BACKEND
        backends.push_back(BackendId::Win32);
#endif
#ifdef LWS_HAS_WAYLAND_BACKEND
        backends.push_back(BackendId::Wayland);
#endif
        return backends;
    }

    void PlatformContext::RunMessageLoop()
    {
        AssertCurrentThread();
        assert(IsUsable());
        const Impl::DispatchScope dispatch(impl_->dispatchDepth);
        impl_->backend->RunMessageLoop(*this);
    }

    bool PlatformContext::ProcessMessages()
    {
        AssertCurrentThread();
        assert(IsUsable());
        const Impl::DispatchScope dispatch(impl_->dispatchDepth);
        return impl_->backend->ProcessMessages(*this);
    }

    Result PlatformContext::PostTask(std::move_only_function<void()> task)
    {
        if (!task)
            return Result::InvalidArgument;

        const std::scoped_lock lock(impl_->taskMutex);
        if (!impl_->acceptingTasks || impl_->backend == nullptr)
            return Result::InvalidState;
        impl_->tasks.push_back(std::move(task));
        if (!impl_->wakePending)
        {
            impl_->wakePending = true;
            const Result wakeResult = impl_->backend->Wake();
            if (wakeResult != Result::Success)
            {
                impl_->wakePending = false;
                impl_->tasks.pop_back();
                return wakeResult;
            }
        }
        return Result::Success;
    }

    void PlatformContext::RequestQuit()
    {
        const std::scoped_lock lock(impl_->taskMutex);
        impl_->quitRequested = true;
        if (!impl_->wakePending && impl_->backend != nullptr)
        {
            impl_->wakePending = impl_->backend->Wake() == Result::Success;
        }
    }

    void PlatformContext::SetUnhandledExceptionHandler(UnhandledExceptionHandler handler)
    {
        AssertCurrentThread();
        impl_->exceptionHandler = handler ? std::make_shared<UnhandledExceptionHandler>(std::move(handler)) : nullptr;
    }

    std::expected<bool, Result> PlatformContext::Supports(PlatformFeature feature) const
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->Supports(feature);
    }

    std::expected<bool, Result> PlatformContext::IsKeyPressed(KeyCode key) const
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->IsKeyPressed(key);
    }

    std::expected<bool, Result> PlatformContext::IsKeyToggled(KeyCode key) const
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->IsKeyToggled(key);
    }

    std::expected<Point, Result> PlatformContext::GetMousePosition() const
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        if (!impl_->backend->Supports(PlatformFeature::GlobalPointerPosition))
            return std::unexpected(Result::NotSupported);
        return impl_->backend->GetMousePosition();
    }

    Result PlatformContext::MoveMouse(Point delta)
    {
        AssertCurrentThread();
        if (!IsUsable())
            return Result::InvalidState;
        if (!impl_->backend->Supports(PlatformFeature::PointerWarp))
            return Result::NotSupported;
        return impl_->backend->MoveMouse(delta);
    }

    Result PlatformContext::RefreshMonitors()
    {
        AssertCurrentThread();
        return IsUsable() ? impl_->backend->RefreshMonitors() : Result::InvalidState;
    }

    std::expected<MonitorDesc, Result> PlatformContext::GetMonitorInfo(uintptr_t handle, bool allowRefresh)
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->GetMonitorInfo(handle, allowRefresh);
    }

    std::expected<MonitorDesc, Result> PlatformContext::GetPrimaryMonitor(bool allowRefresh)
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->GetPrimaryMonitor(allowRefresh);
    }

    std::expected<Rect, Result> PlatformContext::GetBoundingMonitorArea() const
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        return impl_->backend->GetBoundingMonitorArea();
    }

    void PlatformContext::AssertCurrentThread() const noexcept
    {
        assert(IsCurrentThread());
    }

    void PlatformContext::InvokeUserTask(std::move_only_function<void()>& task) noexcept
    {
        try
        {
            task();
        }
        catch (...)
        {
            ReportUnhandledException(std::current_exception());
        }
    }

    void PlatformContext::ReportUnhandledException(std::exception_ptr exception) noexcept
    {
        const auto handler = impl_->exceptionHandler;
        if (handler != nullptr)
            (*handler)(std::move(exception));
        else
            std::terminate();
    }

    void PlatformContext::RegisterWindow()
    {
        AssertCurrentThread();
        assert(IsUsable());
        ++impl_->boundWindowCount;
    }

    void PlatformContext::UnregisterWindow()
    {
        AssertCurrentThread();
        assert(impl_->boundWindowCount != 0);
        --impl_->boundWindowCount;
    }

    void PlatformContext::RegisterService()
    {
        AssertCurrentThread();
        assert(IsUsable());
        ++impl_->boundServiceCount;
    }

    void PlatformContext::UnregisterService()
    {
        AssertCurrentThread();
        assert(impl_->boundServiceCount != 0);
        --impl_->boundServiceCount;
    }
}  // namespace LWS

namespace LWS::internal
{
    void PlatformContextAccess::DrainTasks(PlatformContext& context)
    {
        std::vector<std::move_only_function<void()>> tasks;
        {
            const std::scoped_lock lock(context.impl_->taskMutex);
            if (context.impl_->backend != nullptr)
                context.impl_->backend->ClearWake();
            context.impl_->wakePending = false;
            tasks.swap(context.impl_->tasks);
        }
        for (auto& task : tasks)
            context.InvokeUserTask(task);
    }

    bool PlatformContextAccess::QuitRequested(const PlatformContext& context)
    {
        const std::scoped_lock lock(context.impl_->taskMutex);
        return context.impl_->quitRequested;
    }

    std::unique_ptr<IWindowBackend> PlatformContextAccess::CreateWindowBackend(PlatformContext& context, Window& owner)
    {
        context.AssertCurrentThread();
        assert(context.IsUsable());
        return context.impl_->backend->CreateWindowBackend(owner);
    }
}  // namespace LWS::internal
