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
            Failed,
            Stopped
        };

        explicit Impl(std::thread::id threadId) : threadId(threadId) {}

        const std::thread::id threadId;
        State state{State::Uninitialized};
        std::optional<BackendId> backendId;
        std::optional<BackendFailure> failure;
        std::unique_ptr<internal::PlatformBackend> backend;
        std::mutex taskMutex;
        std::vector<std::move_only_function<void()>> tasks;
        std::vector<std::move_only_function<void()>> spareTasks;  // Always empty; protected by taskMutex.
        bool acceptingTasks{};
        bool wakePending{};
        bool quitRequested{};
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

        auto backend = internal::CreatePlatformBackend(config.backend, *this);
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
        if (impl_->state == Impl::State::Uninitialized || impl_->state == Impl::State::Stopped ||
            impl_->backend == nullptr)
            return Result::Success;
        internal::PlatformContextAccess::DiscardFailedTasks(*this);
        if (impl_->backend == nullptr)
            return Result::Success;
        if (impl_->dispatchDepth != 0 || impl_->boundWindowCount != 0 || impl_->boundServiceCount != 0)
            return Result::InvalidState;

        {
            const std::scoped_lock lock(impl_->taskMutex);
            impl_->acceptingTasks = false;
        }
        {
            const internal::PlatformContextAccess::DispatchScope dispatch(*this);
            internal::PlatformContextAccess::DrainTasks(*this);
        }
        internal::PlatformContextAccess::DiscardFailedTasks(*this);
        std::unique_ptr<internal::PlatformBackend> backend;
        {
            const std::scoped_lock lock(impl_->taskMutex);
            // Accepted tasks may have created a persistent window or service while draining.
            if (impl_->boundWindowCount != 0 || impl_->boundServiceCount != 0)
            {
                impl_->acceptingTasks = !impl_->failure.has_value();
                return Result::InvalidState;
            }
            backend = std::move(impl_->backend);
        }
        if (backend != nullptr)
            backend->Shutdown();
        if (!impl_->failure.has_value())
            impl_->state = Impl::State::Stopped;
        if (activeContext == this)
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

    LoopResult PlatformContext::RunMessageLoop()
    {
        AssertCurrentThread();
        const auto result = internal::PlatformContextAccess::GetLoopResult(*this);
        if (result != LoopResult::Continue)
            return result;
        assert(IsUsable());
        LoopResult outcome;
        {
            const internal::PlatformContextAccess::DispatchScope dispatch(*this);
            outcome = impl_->backend->RunMessageLoop(*this);
        }
        internal::PlatformContextAccess::DiscardFailedTasks(*this);
        return outcome;
    }

    LoopResult PlatformContext::ProcessMessages()
    {
        AssertCurrentThread();
        const auto result = internal::PlatformContextAccess::GetLoopResult(*this);
        if (result != LoopResult::Continue)
            return result;
        assert(IsUsable());
        LoopResult outcome;
        {
            const internal::PlatformContextAccess::DispatchScope dispatch(*this);
            outcome = impl_->backend->ProcessMessages(*this);
        }
        internal::PlatformContextAccess::DiscardFailedTasks(*this);
        return outcome;
    }

    const std::optional<BackendFailure>& PlatformContext::GetFailure() const
    {
        AssertCurrentThread();
        return impl_->failure;
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
        auto monitor = impl_->backend->GetMonitorInfo(handle, allowRefresh);
        return IsUsable() ? std::expected<MonitorDesc, Result>(std::move(monitor)) : std::unexpected(Result::Failure);
    }

    std::expected<MonitorDesc, Result> PlatformContext::GetPrimaryMonitor(bool allowRefresh)
    {
        AssertCurrentThread();
        if (!IsUsable())
            return std::unexpected(Result::InvalidState);
        auto monitor = impl_->backend->GetPrimaryMonitor(allowRefresh);
        return IsUsable() ? std::expected<MonitorDesc, Result>(std::move(monitor)) : std::unexpected(Result::Failure);
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
        if (!IsUsable())
            return;
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
    PlatformContextAccess::DispatchScope::DispatchScope(PlatformContext& context) : context_(context)
    {
        context_.AssertCurrentThread();
        ++context_.impl_->dispatchDepth;
    }
    PlatformContextAccess::DispatchScope::~DispatchScope()
    {
        --context_.impl_->dispatchDepth;
    }

    void PlatformContextAccess::Fail(PlatformContext& context, int nativeError, std::string_view operation)
    {
        context.AssertCurrentThread();
        if (context.impl_->failure.has_value())
            return;
        // Publish failure without destroying captures: a queued callable may own an object still dispatching.
        // Release that ownership only at a safe loop return or during later explicit shutdown.
        context.impl_->failure.emplace(BackendFailure{nativeError, std::string(operation)});
        context.impl_->state = PlatformContext::Impl::State::Failed;
        const std::scoped_lock lock(context.impl_->taskMutex);
        context.impl_->acceptingTasks = false;
        context.impl_->quitRequested = true;
    }

    LoopResult PlatformContextAccess::GetLoopResult(const PlatformContext& context)
    {
        context.AssertCurrentThread();
        if (context.impl_->failure.has_value())
            return LoopResult::Failed;
        return QuitRequested(context) ? LoopResult::Quit : LoopResult::Continue;
    }

    void PlatformContextAccess::DiscardFailedTasks(PlatformContext& context)
    {
        if (!context.impl_->failure.has_value() || context.impl_->dispatchDepth != 0)
            return;
        std::vector<std::move_only_function<void()>> discarded;
        {
            const std::scoped_lock lock(context.impl_->taskMutex);
            discarded.swap(context.impl_->tasks);
        }
        // Destructors run without the queue mutex, and may release windows/services or attempt another post.
    }

    void PlatformContextAccess::DrainTasks(PlatformContext& context)
    {
        std::vector<std::move_only_function<void()>> tasks;
        {
            const std::scoped_lock lock(context.impl_->taskMutex);
            if (context.impl_->backend != nullptr)
                context.impl_->backend->ClearWake();
            context.impl_->wakePending = false;
            tasks.swap(context.impl_->spareTasks);
            tasks.swap(context.impl_->tasks);
        }
        // Consume the complete snapshot: callers do expensive work on background threads and post short callbacks
        // to apply ready results. Keeping UI work brief is the caller's responsibility; this drain has no time budget.
        for (auto& task : tasks)
            context.InvokeUserTask(task);
        if (context.impl_->failure.has_value())
        {
            // Failure has synchronized with producers and closed acceptance. Move outside the mutex:
            // a callable's moved-from destructor may reenter the context.
            for (auto& task : tasks)
                context.impl_->tasks.push_back(std::move(task));
            return;
        }
        // Capture destruction may post or drain recursively. Keep each active batch local, and retire captures
        // outside the mutex before recycling its now-empty storage.
        tasks.clear();
        {
            const std::scoped_lock lock(context.impl_->taskMutex);
            if (context.impl_->tasks.empty() && tasks.capacity() > context.impl_->tasks.capacity())
                tasks.swap(context.impl_->tasks);
            else if (tasks.capacity() > context.impl_->spareTasks.capacity())
                tasks.swap(context.impl_->spareTasks);
        }
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
