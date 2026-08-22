#pragma once

#include <LWS/interfaces/backends.hpp>

#include <cstdint>
#include <functional>
#include <memory>

namespace LWS
{
    class Timer
    {
    public:
        using Callback = std::function<void()>;
        class Impl;

        Timer();
        ~Timer();

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
        Timer(Timer&&) noexcept = delete;
        Timer& operator=(Timer&&) noexcept = delete;

        void SetTargetWindow(Handle windowHandle);
        [[nodiscard]] uint32_t GetInterval() const;
        void SetInterval(uint32_t interval);
        void SetCallback(Callback callback);

    private:
        std::unique_ptr<Impl> impl_;
    };

    class HighPrecisionTimer
    {
    public:
        using Callback = std::function<void()>;
        class Impl;

        explicit HighPrecisionTimer(Callback callback);
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
        std::unique_ptr<Impl> impl_;
    };
}
