#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <LWS/Timer.hpp>
    #include <LWS/interfaces/backends.hpp>
    #include <LLUtils/Exception.h>

    #include <atomic>
    #include <map>
    #include <memory>
    #include <mutex>
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

        static void CALLBACK TimerProc(HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime);

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

        void setTargetWindow(Handle handle) override { SetTargetWindow(handle); }
        uint32_t getInterval() const override { return GetInterval(); }
        void setInterval(uint32_t interval) override { SetInterval(interval); }
        void setCallback(Callback callback) override { SetCallback(std::move(callback)); }

        void SetTargetWindow(Handle hwnd)
        {
            const uint32_t interval = fInterval;
            Unregister();
            fWindowHandle = reinterpret_cast<HWND>(hwnd);
            fInterval = 0;
            if (fWindowHandle != nullptr)
                SetInterval(interval);
            else
                fInterval = interval;
        }

        uint32_t GetInterval() const { return fInterval; }

        void SetInterval(uint32_t interval)
        {
            if (fInterval == interval)
                return;

            if (interval == 0)
                Unregister();
            else if (fWindowHandle != nullptr)
            {
                if (fTimerID == 0)
                    fTimerID = TimerManager::Get().RegisterTimer(*this);
                if (SetTimer(fWindowHandle, fTimerID, interval, TimerManager::TimerProc) == 0)
                {
                    Unregister();
                    fInterval = 0;
                    LL_EXCEPTION_SYSTEM_ERROR("Could not create timer");
                }
            }
            fInterval = interval;
        }

        void SetCallback(Callback callback)
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
        }

        void SetRepeatInterval(uint32_t repeatInterval)
        {
            if (fRepeatInterval != repeatInterval)
            {
                fRepeatInterval = repeatInterval;
                if (fEnabled)
                {
                    Enable(false);
                    Enable(true);
                }
            }
        }

        void SetDueTime(uint32_t dueTime) { fDueTime = dueTime; }

        bool GetEnabled() const { return fEnabled; }

        void Enable(bool enable)
        {
            if (enable == fEnabled)
                return;

            Stop();
            if (enable)
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

        void setRepeatInterval(uint32_t interval) override { SetRepeatInterval(interval); }
        void setDueTime(uint32_t dueTime) override { SetDueTime(dueTime); }
        bool getEnabled() const override { return GetEnabled(); }
        void enable(bool enabled) override { Enable(enabled); }

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
            MSG message{};
            while (PeekMessageW(&message, fWindowHandle, ON_TIMER_MESSAGE, ON_TIMER_MESSAGE, PM_REMOVE))
            {
            }
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
                auto* timer = reinterpret_cast<HighPrecisionTimerBackendWin32*>(
                    GetWindowLongPtrW(hWnd, GWLP_USERDATA));
                if (timer != nullptr)
                    timer->ExecuteTimerFuncThreadSafe();
                return 0;
            }
            return DefWindowProcW(hWnd, msg, wParam, lParam);
        }

        void CreateWindowClassOnce()
        {
            std::call_once(fCreateClassOnceFlag,
                           []()
                           {
                               WNDCLASS wc{};
                               wc.lpfnWndProc = WindowProc;
                               wc.hInstance = GetModuleHandle(nullptr);
                               wc.lpszClassName = CLASS_NAME;
                               if (RegisterClass(&wc) == 0)
                               {
                                   LL_EXCEPTION_SYSTEM_ERROR("Could not create window class");
                               }
                           });
        }

        void RegisterWindow()
        {
            CreateWindowClassOnce();
            fWindowHandle = CreateWindowExW(0, CLASS_NAME, nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                            GetModuleHandleW(nullptr), this);
            if (fWindowHandle == nullptr)
                LL_EXCEPTION_SYSTEM_ERROR("Could not create timer window");
        }

        static constexpr LWS::char_type CLASS_NAME[] = L"LWS.HighPrecisionTimerWindow";
        static constexpr UINT ON_TIMER_MESSAGE = WM_USER + 1;
        static inline std::once_flag fCreateClassOnceFlag;
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
    void CALLBACK TimerManager::TimerProc(HWND window, UINT, UINT_PTR idTimer, DWORD)
    {
        TimerManager& manager = TimerManager::Get();
        const auto it = manager.fMapTimerIdToTimer.find(idTimer);
        if (it != manager.fMapTimerIdToTimer.end())
            it->second->Execute(window);
    }
}  // namespace
#endif
