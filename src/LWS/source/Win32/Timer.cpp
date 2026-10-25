#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include "internal/SharedWindowClass.hpp"
    #include "internal/MessageCleanup.hpp"
    #include <LWS/Timer.hpp>
    #include <LWS/source/internal/Backends.hpp>
    #include <LLUtils/Exception.h>

    #include <atomic>
    #include <map>
    #include <memory>
    #include <tuple>
    #include <utility>

namespace LWS
{
    class TimerBackendWin32;
}

namespace
{
    class TimerManager
    {
      public:

        using TimerIDType = UINT_PTR;

        static TimerManager& Get()
        {
            thread_local TimerManager manager;
            return manager;
        }

        TimerIDType RegisterTimer(const LWS::TimerBackendWin32& timer)
        {
            if (fNextID == 0)
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Timer identifiers exhausted");
            const TimerIDType id = fNextID++;
            fMapTimerIdToTimer.emplace(id, &timer);
            return id;
        }

        void UnRegisterTimer(TimerIDType timerID) { fMapTimerIdToTimer.erase(timerID); }

        static void Dispatch(HWND window, UINT_PTR idTimer);

      private:

        // Never recycle an ID: KillTimer does not remove already retrieved WM_TIMER messages.
        TimerIDType fNextID = 1;
        std::map<TimerIDType, const LWS::TimerBackendWin32*> fMapTimerIdToTimer;
    };
}  // namespace

namespace LWS
{
    class TimerBackendWin32 final : public internal::ITimerBackend
    {
      public:

        ~TimerBackendWin32() override { Unregister(); }

        void setTargetWindow(Handle hwnd) override
        {
            const uint32_t interval = fInterval;
            Unregister();
            fWindowHandle = reinterpret_cast<HWND>(hwnd);
            fInterval = 0;
            if (fWindowHandle != nullptr)
                setInterval(interval);
            else
                fInterval = interval;
        }

        uint32_t getInterval() const override { return fInterval; }

        void setInterval(uint32_t interval) override
        {
            if (fInterval == interval)
                return;

            if (interval == 0)
                Unregister();
            else if (fWindowHandle != nullptr)
            {
                if (fTimerID == 0)
                    fTimerID = TimerManager::Get().RegisterTimer(*this);
                // A retrieved WM_TIMER with TIMERPROC retains a DLL code address even after KillTimer.
                // Dispatch through the live target window instead, so stale messages cannot enter
                // an unloaded module after its windows have been destroyed.
                if (SetTimer(fWindowHandle, fTimerID, interval, nullptr) == 0)
                {
                    Unregister();
                    fInterval = 0;
                    LL_EXCEPTION_SYSTEM_ERROR("Could not create timer");
                }
            }
            fInterval = interval;
        }

        void setCallback(Callback callback) override
        {
            fCallback = callback ? std::make_shared<Callback>(std::move(callback)) : nullptr;
        }

        void Execute(HWND window) const
        {
            if (window != fWindowHandle || !IsWindow(window))
                return;
            const auto callback = fCallback;
            if (callback != nullptr)
                (*callback)();
        }

      private:

        void Unregister()
        {
            if (fTimerID != 0)
            {
                KillTimer(fWindowHandle, fTimerID);
                TimerManager::Get().UnRegisterTimer(fTimerID);
                fTimerID = 0;
            }
        }

        std::shared_ptr<Callback> fCallback;
        TimerManager::TimerIDType fTimerID = 0;
        uint32_t fInterval = 0;
        HWND fWindowHandle = nullptr;
    };

    class HighPrecisionTimerBackendWin32 final : public internal::IHighPrecisionTimerBackend
    {
      public:

        explicit HighPrecisionTimerBackendWin32(internal::ITimerBackend::Callback callback)
            : fCallback(callback ? std::make_shared<internal::ITimerBackend::Callback>(std::move(callback)) : nullptr)
        {
            RegisterWindow();
        }

        ~HighPrecisionTimerBackendWin32() override
        {
            Stop();
            DestroyWindow(fWindowHandle);
            fClass.Release();
        }

        void setRepeatInterval(uint32_t repeatInterval) override
        {
            if (fRepeatInterval != repeatInterval)
            {
                fRepeatInterval = repeatInterval;
                if (fEnabled)
                {
                    enable(false);
                    enable(true);
                }
            }
        }

        void setDueTime(uint32_t dueTime) override { fDueTime = dueTime; }

        bool getEnabled() const override { return fEnabled; }

        void enable(bool enabled) override
        {
            if (enabled == fEnabled)
                return;

            Stop();
            if (enabled)
            {
                fEnabled = true;
                const uint32_t period = fRepeatInterval == INFINITE ? 0 : fRepeatInterval;
                if (CreateTimerQueueTimer(&fTimerID, nullptr, OnTimer, this, fDueTime, period,
                                          WT_EXECUTEINTIMERTHREAD) == FALSE)
                {
                    fEnabled = false;
                    LL_EXCEPTION_SYSTEM_ERROR("Could not create timer");
                }
            }
        }

      private:

        static VOID CALLBACK OnTimer(PVOID parameter, BOOLEAN)
        {
            reinterpret_cast<HighPrecisionTimerBackendWin32*>(parameter)->ExecuteTimerFunc();
        }

        void ExecuteTimerFunc()
        {
            // Coalesce ticks while the UI is busy; the timer thread must never wait for the UI thread.
            if (!fTickPending.exchange(true) && !PostMessageW(fWindowHandle, ON_TIMER_MESSAGE, 0, 0))
                fTickPending = false;
        }

        void ExecuteTimerFuncThreadSafe()
        {
            fTickPending = false;
            if (!fEnabled)
                return;
            if (fRepeatInterval == INFINITE || fRepeatInterval == 0)
                fEnabled = false;
            const auto callback = fCallback;
            if (callback != nullptr)
                (*callback)();
        }

        void Stop()
        {
            fEnabled = false;
            if (fTimerID != nullptr)
            {
                std::ignore = DeleteTimerQueueTimer(nullptr, fTimerID, INVALID_HANDLE_VALUE);
                fTimerID = nullptr;
            }
            const internal::MessageCleanup cleanup(fWindowHandle, ON_TIMER_MESSAGE);
            fTickPending = false;
        }

        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            if (msg == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }
            else if (msg == ON_TIMER_MESSAGE)
            {
                auto* timer = reinterpret_cast<HighPrecisionTimerBackendWin32*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
                if (timer != nullptr)
                    timer->ExecuteTimerFuncThreadSafe();
                return 0;
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        void RegisterWindow()
        {
            if (!fClass.Acquire())
                LL_EXCEPTION_SYSTEM_ERROR("Could not register timer window class");
            fWindowHandle = CreateWindowExW(0, fClass.Name(), nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                            fClass.Module(), this);
            if (fWindowHandle == nullptr)
            {
                fClass.Release();
                LL_EXCEPTION_SYSTEM_ERROR("Could not create timer window");
            }
        }

        static constexpr UINT ON_TIMER_MESSAGE = WM_USER + 1;
        static inline internal::SharedWindowClass fClass{L"LWS.HighPrecisionTimerWindow", WindowProc};
        std::atomic_bool fTickPending = false;
        bool fEnabled = false;
        HANDLE fTimerID = nullptr;
        std::shared_ptr<internal::ITimerBackend::Callback> fCallback;
        uint32_t fDueTime = INFINITE;
        uint32_t fRepeatInterval = INFINITE;
        HWND fWindowHandle = nullptr;
    };

}  // namespace LWS

namespace LWS::internal
{
    void dispatchWindowTimer(HWND window, UINT_PTR id)
    {
        TimerManager::Dispatch(window, id);
    }

    std::unique_ptr<ITimerBackend> createTimerBackend(PlatformContext&)
    {
        return std::make_unique<TimerBackendWin32>();
    }
    std::unique_ptr<IHighPrecisionTimerBackend> createHighPrecisionTimerBackend(PlatformContext&,
                                                                                ITimerBackend::Callback callback)
    {
        return std::make_unique<HighPrecisionTimerBackendWin32>(std::move(callback));
    }
}  // namespace LWS::internal

namespace
{
    void TimerManager::Dispatch(HWND window, UINT_PTR idTimer)
    {
        TimerManager& manager = TimerManager::Get();
        const auto it = manager.fMapTimerIdToTimer.find(idTimer);
        if (it != manager.fMapTimerIdToTimer.end())
            it->second->Execute(window);
    }
}  // namespace
#endif
