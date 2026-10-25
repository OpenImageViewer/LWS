#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Timer.hpp>
    #include <LWS/source/internal/Backends.hpp>

    #include "internal/PlatformState.hpp"

    #include <cerrno>
    #include <limits>
    #include <system_error>
    #include <sys/timerfd.h>
    #include <unistd.h>
    #include <utility>

namespace LWS
{
    namespace
    {
        class NativeTimer
        {
          public:

            explicit NativeTimer(PlatformContext& platform)
                : platform_(*static_cast<internal::WaylandPlatformState*>(
                      internal::PlatformContextAccess::GetBackend(platform)))
            {
            }
            ~NativeTimer() { stop(); }
            NativeTimer(const NativeTimer&) = delete;
            NativeTimer& operator=(const NativeTimer&) = delete;

            void setCallback(Timer::Callback callback)
            {
                callback_ = callback ? std::make_shared<Timer::Callback>(std::move(callback)) : nullptr;
            }

            void start(uint32_t dueTime, uint32_t repeatInterval)
            {
                stop();
                if (dueTime == std::numeric_limits<uint32_t>::max())
                    return;
                descriptor_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC | TFD_NONBLOCK);
                if (descriptor_ < 0)
                    throw std::system_error(errno, std::generic_category(), "timerfd_create");
                try
                {
                    itimerspec interval{};
                    interval.it_value = duration(dueTime);
                    // A zero timerfd deadline disarms it; the public API means fire immediately.
                    if (dueTime == 0)
                        interval.it_value.tv_nsec = 1;
                    if (repeatInterval != std::numeric_limits<uint32_t>::max())
                        interval.it_interval = duration(repeatInterval);
                    if (timerfd_settime(descriptor_, 0, &interval, nullptr) < 0)
                        throw std::system_error(errno, std::generic_category(), "timerfd_settime");
                    platform_.registerTimer(descriptor_,
                                            [this]
                                            {
                                                // The callback may replace itself or destroy its timer.
                                                const auto callback = callback_;
                                                if (callback)
                                                    (*callback)();
                                            });
                }
                catch (...)
                {
                    stop();
                    throw;
                }
            }

            void stop()
            {
                if (descriptor_ >= 0)
                {
                    platform_.unregisterTimer(descriptor_);
                    close(std::exchange(descriptor_, -1));
                }
            }

          private:

            static timespec duration(uint32_t milliseconds)
            {
                return {.tv_sec = milliseconds / 1000, .tv_nsec = long(milliseconds % 1000) * 1'000'000};
            }
            internal::WaylandPlatformState& platform_;
            std::shared_ptr<Timer::Callback> callback_;
            int descriptor_ = -1;
        };
    }  // namespace

    class TimerBackendWayland final : public internal::ITimerBackend
    {
      public:

        explicit TimerBackendWayland(PlatformContext& platform) : fTimer(platform) {}

        void setTargetWindow(Handle handle) override
        {
            fTimer.stop();
            const auto interval = std::exchange(fInterval, 0);
            fDetached = handle == 0;
            if (!fDetached && interval != 0)
                fTimer.start(interval, interval);
            fInterval = interval;
        }
        uint32_t getInterval() const override { return fInterval; }
        void setCallback(Callback callback) override { fTimer.setCallback(std::move(callback)); }

        void setInterval(uint32_t interval) override
        {
            if (fInterval == interval)
                return;
            fTimer.stop();
            fInterval = 0;
            if (interval != 0 && !fDetached)
                fTimer.start(interval, interval);
            fInterval = interval;
        }

      private:

        NativeTimer fTimer;
        uint32_t fInterval = 0;
        bool fDetached = false;
    };

    class HighPrecisionTimerBackendWayland final : public internal::IHighPrecisionTimerBackend
    {
      public:

        HighPrecisionTimerBackendWayland(PlatformContext& platform, internal::ITimerBackend::Callback callback)
            : fTimer(platform)
        {
            fTimer.setCallback(std::move(callback));
        }

        void enable(bool enabled) override
        {
            if (enabled == fEnabled)
                return;
            fTimer.stop();
            fEnabled = false;
            if (enabled)
            {
                fTimer.start(fDueTime, fRepeatInterval);
                fEnabled = true;
            }
        }

        void setRepeatInterval(uint32_t repeatInterval) override
        {
            if (fRepeatInterval != repeatInterval)
            {
                fRepeatInterval = repeatInterval;
                restartIfEnabled();
            }
        }

        void setDueTime(uint32_t dueTime) override
        {
            if (fDueTime != dueTime)
            {
                fDueTime = dueTime;
                restartIfEnabled();
            }
        }

        bool getEnabled() const override { return fEnabled; }

      private:

        void restartIfEnabled()
        {
            if (fEnabled)
            {
                enable(false);
                enable(true);
            }
        }

        NativeTimer fTimer;
        bool fEnabled = false;
        uint32_t fDueTime = std::numeric_limits<uint32_t>::max();
        uint32_t fRepeatInterval = std::numeric_limits<uint32_t>::max();
    };

    namespace internal
    {
        std::unique_ptr<ITimerBackend> createTimerBackend(PlatformContext& platform)
        {
            return std::make_unique<TimerBackendWayland>(platform);
        }

        std::unique_ptr<IHighPrecisionTimerBackend> createHighPrecisionTimerBackend(PlatformContext& platform,
                                                                                    ITimerBackend::Callback callback)
        {
            return std::make_unique<HighPrecisionTimerBackendWayland>(platform, std::move(callback));
        }
    }  // namespace internal
}  // namespace LWS

#endif
