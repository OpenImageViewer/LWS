#pragma once

#include <LWS/Platform.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace LWS
{
    class Timer
    {
      public:

        using Callback = std::function<void()>;
        explicit Timer(PlatformContext& platform);
        ~Timer();

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
        Timer(Timer&&) noexcept = delete;
        Timer& operator=(Timer&&) noexcept = delete;

        /// A null target or target destruction detaches the timer while preserving its configured interval.
        [[nodiscard]] Result SetTargetWindow(Window* window);
        [[nodiscard]] uint32_t GetInterval() const;
        void SetInterval(uint32_t interval);
        void SetCallback(Callback callback);

      private:

        PlatformContext& platform_;
        class Impl;
        std::unique_ptr<Impl> impl_;
    };

    class HighPrecisionTimer
    {
      public:

        using Callback = std::function<void()>;
        HighPrecisionTimer(PlatformContext& platform, Callback callback);
        ~HighPrecisionTimer();

        HighPrecisionTimer(const HighPrecisionTimer&) = delete;
        HighPrecisionTimer& operator=(const HighPrecisionTimer&) = delete;
        HighPrecisionTimer(HighPrecisionTimer&&) noexcept = delete;
        HighPrecisionTimer& operator=(HighPrecisionTimer&&) noexcept = delete;

        void SetRepeatInterval(uint32_t repeatInterval);
        void SetDueTime(uint32_t dueTime);
        [[nodiscard]] bool GetEnabled() const;
        void Enable(bool enable);

      private:

        PlatformContext& platform_;
        class Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWS
