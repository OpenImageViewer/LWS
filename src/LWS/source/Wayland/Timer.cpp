#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Timer.hpp>
    #include <LWS/interfaces/backends.hpp>

    #include <atomic>
    #include <chrono>
    #include <condition_variable>
    #include <limits>
    #include <mutex>
    #include <thread>

namespace LWS
{
    namespace
    {
        struct CallbackState
        {
            PlatformContext* platform = nullptr;
            std::shared_ptr<Timer::Callback> callback;
            std::atomic_bool enabled = false;
            std::atomic_bool pending = false;
            std::atomic_uint64_t generation = 0;
        };

        void postCallback(const std::shared_ptr<CallbackState>& state)
        {
            if (state->pending.exchange(true))
                return;
            const uint64_t generation = state->generation;
            const Result result = state->platform->PostTask(
                [weakState = std::weak_ptr(state), generation]
                {
                    const auto locked = weakState.lock();
                    if (locked == nullptr || locked->generation != generation)
                        return;
                    locked->pending = false;
                    if (!locked->enabled)
                        return;
                    const auto callback = locked->callback;
                    if (callback != nullptr)
                        (*callback)();
                });
            if (result != Result::Success)
                state->pending = false;
        }

        void cancelCallbacks(const std::shared_ptr<CallbackState>& state)
        {
            ++state->generation;
            state->pending = false;
        }

        std::jthread startTimerThread(const std::shared_ptr<CallbackState>& state, uint32_t dueTime,
                                      uint32_t repeatInterval)
        {
            return std::jthread(
                [state, dueTime, repeatInterval](std::stop_token stopToken)
                {
                    std::condition_variable_any wake;
                    std::mutex waitMutex;
                    auto wait = [&](uint32_t milliseconds)
                    {
                        std::unique_lock lock(waitMutex);
                        return !wake.wait_for(lock, stopToken, std::chrono::milliseconds(milliseconds),
                                              [] { return false; });
                    };

                    bool fire = dueTime != std::numeric_limits<uint32_t>::max() && wait(dueTime);
                    while (fire && state->enabled && !stopToken.stop_requested())
                    {
                        postCallback(state);
                        fire = repeatInterval != 0 && repeatInterval != std::numeric_limits<uint32_t>::max() &&
                               wait(repeatInterval);
                    }
                });
        }
    }  // namespace

    class TimerBackendWayland final : public internal::ITimerBackend
    {
      public:

        explicit TimerBackendWayland(PlatformContext& platform) : fState(std::make_shared<CallbackState>())
        {
            fState->platform = &platform;
        }

        ~TimerBackendWayland() override { stop(); }

        void setTargetWindow(Handle handle) override
        {
            stop();
            fDetached = handle == 0;
            if (!fDetached && fInterval != 0)
            {
                fState->enabled = true;
                fThread = startTimerThread(fState, fInterval, fInterval);
            }
        }
        uint32_t getInterval() const override { return fInterval; }
        void setCallback(Callback callback) override
        {
            fState->callback = callback ? std::make_shared<Callback>(std::move(callback)) : nullptr;
        }

        void setInterval(uint32_t interval) override
        {
            if (fInterval != interval)
            {
                stop();
                fInterval = interval;
                if (interval != 0 && !fDetached)
                {
                    fState->enabled = true;
                    fThread = startTimerThread(fState, interval, interval);
                }
            }
        }

        void stop()
        {
            fState->enabled = false;
            if (fThread.joinable())
            {
                fThread.request_stop();
                fThread.join();
            }
            cancelCallbacks(fState);
        }

        std::shared_ptr<CallbackState> fState;
        std::jthread fThread;
        uint32_t fInterval = 0;
        bool fDetached = false;
    };

    class HighPrecisionTimerBackendWayland final : public internal::IHighPrecisionTimerBackend
    {
      public:

        HighPrecisionTimerBackendWayland(PlatformContext& platform, internal::ITimerBackend::Callback callback)
            : fState(std::make_shared<CallbackState>())
        {
            fState->platform = &platform;
            fState->callback = callback ? std::make_shared<Timer::Callback>(std::move(callback)) : nullptr;
        }

        ~HighPrecisionTimerBackendWayland() override { enable(false); }

        void enable(bool enabled) override
        {
            if (enabled == fState->enabled)
            {
                return;
            }
            fState->enabled = false;
            if (fThread.joinable())
            {
                fThread.request_stop();
                fThread.join();
            }
            cancelCallbacks(fState);
            if (enabled)
            {
                fState->enabled = true;
                fThread = startTimerThread(fState, fDueTime, fRepeatInterval);
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

        bool getEnabled() const override { return fState->enabled; }

        void restartIfEnabled()
        {
            if (fState->enabled)
            {
                enable(false);
                enable(true);
            }
        }

        std::shared_ptr<CallbackState> fState;
        std::jthread fThread;
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
