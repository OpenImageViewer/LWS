#include <LWS/Timer.hpp>
#include <LWS/source/internal/Backends.hpp>
#include <LWS/Window.hpp>

#include "internal/WindowBackendAccess.hpp"

namespace
{
    // Keep callback lifetime and exception handling identical for both timer backends.
    LWS::Timer::Callback ProtectCallback(LWS::PlatformContext& context, LWS::Timer::Callback callback)
    {
        if (!callback)
            return {};
        return [platform = &context, callback = std::move(callback)]
        {
            const LWS::internal::PlatformContextAccess::DispatchScope dispatch(*platform);
            try
            {
                if (platform->IsUsable())
                    callback();
            }
            catch (...)
            {
                LWS::internal::PlatformContextAccess::ReportUnhandledException(*platform, std::current_exception());
            }
        };
    }
}  // namespace

namespace LWS
{
    class Timer::Impl
    {
      public:

        explicit Impl(PlatformContext& platform)
        {
            platform.AssertCurrentThread();
            backend = internal::createTimerBackend(platform);
        }
        std::unique_ptr<internal::ITimerBackend> backend;
        EventConnection targetConnection;
    };

    Timer::Timer(PlatformContext& platform) : platform_(platform), impl_(std::make_unique<Impl>(platform))
    {
        platform_.RegisterService();
    }

    Timer::~Timer()
    {
        platform_.AssertCurrentThread();
        impl_.reset();
        platform_.UnregisterService();
    }
    Result Timer::SetTargetWindow(Window* window)
    {
        platform_.AssertCurrentThread();
        if (window == nullptr)
        {
            impl_->backend->setTargetWindow(0);
            impl_->targetConnection.Disconnect();
            return Result::Success;
        }
        if (&window->GetPlatformContext() != &platform_)
            return Result::InvalidArgument;
        if (!window->IsCreated())
            return Result::InvalidState;
        // Detach before user listeners can consume the destruction event or release the timer.
        auto connection = window->Listen(
            [this](const AnyEvent& event)
            {
                if ((std::holds_alternative<EventWindowDestroying>(event) ||
                     std::holds_alternative<EventWindowDestroyed>(event)))
                    std::ignore = SetTargetWindow(nullptr);
                return EventResponse::Unhandled;
            },
            true);
        if (!connection.has_value())
            return connection.error();
        impl_->backend->setTargetWindow(internal::WindowBackendAccess::Get(*window)->getHandle());
        impl_->targetConnection = std::move(*connection);
        return Result::Success;
    }
    uint32_t Timer::GetInterval() const
    {
        platform_.AssertCurrentThread();
        return impl_->backend->getInterval();
    }
    void Timer::SetInterval(uint32_t interval)
    {
        platform_.AssertCurrentThread();
        if (platform_.IsUsable() || interval == 0)
            impl_->backend->setInterval(interval);
    }
    void Timer::SetCallback(Callback callback)
    {
        platform_.AssertCurrentThread();
        impl_->backend->setCallback(ProtectCallback(platform_, std::move(callback)));
    }

    HighPrecisionTimer::HighPrecisionTimer(PlatformContext& platform, Callback callback) : platform_(platform)
    {
        platform_.AssertCurrentThread();
        backend_ = internal::createHighPrecisionTimerBackend(platform_,
                                                             ProtectCallback(platform_, std::move(callback)));
        platform_.RegisterService();
    }

    HighPrecisionTimer::~HighPrecisionTimer()
    {
        platform_.AssertCurrentThread();
        backend_.reset();
        platform_.UnregisterService();
    }
    void HighPrecisionTimer::SetRepeatInterval(uint32_t interval)
    {
        platform_.AssertCurrentThread();
        if (platform_.IsUsable())
            backend_->setRepeatInterval(interval);
    }
    void HighPrecisionTimer::SetDueTime(uint32_t dueTime)
    {
        platform_.AssertCurrentThread();
        if (platform_.IsUsable())
            backend_->setDueTime(dueTime);
    }
    bool HighPrecisionTimer::GetEnabled() const
    {
        platform_.AssertCurrentThread();
        return platform_.IsUsable() && backend_->getEnabled();
    }
    void HighPrecisionTimer::Enable(bool enabled)
    {
        platform_.AssertCurrentThread();
        if (platform_.IsUsable() || !enabled)
            backend_->enable(enabled);
    }
}  // namespace LWS
