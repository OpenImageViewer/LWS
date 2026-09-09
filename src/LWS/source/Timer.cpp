#include <LWS/Timer.hpp>
#include <LWS/interfaces/backends.hpp>
#include <LWS/Window.hpp>

#include "internal/WindowBackendAccess.hpp"

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

    class HighPrecisionTimer::Impl
    {
      public:

        Impl(PlatformContext& platform, internal::ITimerBackend::Callback callback)
        {
            platform.AssertCurrentThread();
            backend = internal::createHighPrecisionTimerBackend(platform, std::move(callback));
        }
        std::unique_ptr<internal::IHighPrecisionTimerBackend> backend;
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
                if (std::holds_alternative<EventWindowDestroyed>(event))
                    std::ignore = SetTargetWindow(nullptr);
                return EventResponse::Unhandled;
            }, true);
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
        impl_->backend->setInterval(interval);
    }
    void Timer::SetCallback(Callback callback)
    {
        platform_.AssertCurrentThread();
        if (!callback)
        {
            impl_->backend->setCallback({});
            return;
        }
        impl_->backend->setCallback(
            [platform = &platform_, callback = std::move(callback)]
            {
                try
                {
                    callback();
                }
                catch (...)
                {
                    platform->ReportUnhandledException(std::current_exception());
                }
            });
    }

    HighPrecisionTimer::HighPrecisionTimer(PlatformContext& platform, Callback callback)
        : platform_(platform),
          impl_(std::make_unique<Impl>(platform,
                                       [platform = &platform_, callback = std::move(callback)]
                                       {
                                           try
                                           {
                                               if (callback)
                                                   callback();
                                           }
                                           catch (...)
                                           {
                                               platform->ReportUnhandledException(std::current_exception());
                                           }
                                       }))
    {
        platform_.RegisterService();
    }

    HighPrecisionTimer::~HighPrecisionTimer()
    {
        platform_.AssertCurrentThread();
        impl_.reset();
        platform_.UnregisterService();
    }
    void HighPrecisionTimer::SetRepeatInterval(uint32_t interval)
    {
        platform_.AssertCurrentThread();
        impl_->backend->setRepeatInterval(interval);
    }
    void HighPrecisionTimer::SetDueTime(uint32_t dueTime)
    {
        platform_.AssertCurrentThread();
        impl_->backend->setDueTime(dueTime);
    }
    bool HighPrecisionTimer::GetEnabled() const
    {
        platform_.AssertCurrentThread();
        return impl_->backend->getEnabled();
    }
    void HighPrecisionTimer::Enable(bool enabled)
    {
        platform_.AssertCurrentThread();
        impl_->backend->enable(enabled);
    }
}  // namespace LWS
