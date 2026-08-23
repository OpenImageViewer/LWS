#ifdef LWS_PLATFORM_WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <Windows.h>

    #include <LWS/Timer.hpp>
    #include <LLUtils/Exception.h>
    #include <LLUtils/Singleton.h>
    #include <LLUtils/Templates.h>
    #include <LLUtils/UniqueIDProvider.h>

    #include <map>
    #include <mutex>
    #include <set>
    #include <tuple>
    #include <utility>

namespace
{
    class TimerManager : public LLUtils::Singleton<TimerManager>
    {
      public:

        using TimerIDType = size_t;

        TimerIDType RegisterTimer(const LWS::Timer::Impl& timer)
        {
            TimerIDType id = fUniqueIdProvider.Acquire();
            auto [_, inserted] = fMapTimerIdToTimer.emplace(id, &timer);
            if (!inserted)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::DuplicateItem, "Timer id already registered");
            }
            return id;
        }

        void UnRegisterTimer(TimerIDType timerID)
        {
            auto it = fMapTimerIdToTimer.find(timerID);
            if (it == fMapTimerIdToTimer.end())
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Timer id not found in data structure");
            }

            fMapTimerIdToTimer.erase(it);
            fUniqueIdProvider.Release(timerID);
        }

        static void CALLBACK TimerProc(HWND hwnd, UINT message, UINT_PTR idTimer, DWORD dwTime);

      private:

        using UniqueIdProviderType = LLUtils::UniqueIdProvider<TimerIDType, std::set<TimerIDType>>;
        UniqueIdProviderType fUniqueIdProvider = UniqueIdProviderType(1);
        std::map<TimerIDType, const LWS::Timer::Impl*> fMapTimerIdToTimer;
    };
}  // namespace

namespace LWS
{
    class Timer::Impl
    {
      public:

        ~Impl() { Unregister(); }

        void SetTargetWindow(Handle hwnd)
        {
            if (hwnd == 0)
            {
                LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "null window as timer target is illegal");
            }

            const uint32_t interval = fInterval;
            Unregister();
            fWindowHandle = reinterpret_cast<HWND>(hwnd);
            SetInterval(interval);
        }

        uint32_t GetInterval() const { return fInterval; }

        void SetInterval(uint32_t interval)
        {
            if (fInterval == interval)
            {
                return;
            }

            Register();
            fInterval = interval;
            if (fInterval == 0)
            {
                KillTimer(fWindowHandle, fTimerID);
            }
            else
            {
                SetTimer(fWindowHandle, fTimerID, fInterval, reinterpret_cast<TIMERPROC>(TimerManager::TimerProc));
            }
        }

        void SetCallback(Callback callback) { fCallback = std::move(callback); }

        void Execute() const
        {
            if (fCallback)
            {
                fCallback();
            }
        }

      private:

        void Unregister()
        {
            if (fTimerID != 0)
            {
                SetInterval(0);
                TimerManager::GetSingleton().UnRegisterTimer(fTimerID);
                fTimerID = 0;
            }
        }

        void Register()
        {
            if (fTimerID == 0)
            {
                if (fWindowHandle == nullptr)
                {
                    LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState,
                                 "Timer is not bound to a window, call LWS::Timer::SetTargetWindow first");
                }
                fTimerID = TimerManager::GetSingleton().RegisterTimer(*this);
            }
        }

        Callback fCallback;
        TimerManager::TimerIDType fTimerID = 0;
        uint32_t fInterval = 0;
        HWND fWindowHandle = nullptr;
    };

    Timer::Timer() : impl_(std::make_unique<Impl>()) {}
    Timer::~Timer() = default;
    void Timer::SetTargetWindow(Handle windowHandle)
    {
        impl_->SetTargetWindow(windowHandle);
    }
    uint32_t Timer::GetInterval() const
    {
        return impl_->GetInterval();
    }
    void Timer::SetInterval(uint32_t interval)
    {
        impl_->SetInterval(interval);
    }
    void Timer::SetCallback(Callback callback)
    {
        impl_->SetCallback(std::move(callback));
    }

    class HighPrecisionTimer::Impl
    {
      public:

        explicit Impl(Callback callback) : fCallback(std::move(callback)) { RegisterWindow(); }

        ~Impl()
        {
            Enable(false);
            UnregisterWindow();
            if (fTimerID != nullptr)
            {
                std::ignore = DeleteTimerQueueTimer(nullptr, fTimerID, INVALID_HANDLE_VALUE);
            }
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
            {
                return;
            }

            fEnabled = enable;
            if (fEnabled)
            {
                if (fTimerID == nullptr)
                {
                    if (CreateTimerQueueTimer(&fTimerID, nullptr, OnTimer, reinterpret_cast<PVOID>(this), fDueTime,
                                              fRepeatInterval, WT_EXECUTEINTIMERTHREAD) == FALSE)
                    {
                        LL_EXCEPTION_SYSTEM_ERROR("Could not create timer");
                    }
                }
                else if (ChangeTimerQueueTimer(nullptr, fTimerID, fDueTime, fRepeatInterval) == FALSE)
                {
                    LL_EXCEPTION_SYSTEM_ERROR("Could not reenable timer");
                }
            }
            else if (fTimerID != nullptr && ChangeTimerQueueTimer(nullptr, fTimerID, INFINITE, INFINITE) == FALSE)
            {
                LL_EXCEPTION_SYSTEM_ERROR("Could not disable timer");
            }
        }

      private:

        static VOID CALLBACK OnTimer(PVOID parameter, BOOLEAN)
        {
            reinterpret_cast<Impl*>(parameter)->ExecuteTimerFunc();
        }

        void ExecuteTimerFunc() { SendMessage(fWindowHandle, ON_TIMER_MESSAGE, reinterpret_cast<WPARAM>(this), 0); }

        void ExecuteTimerFuncThreadSafe()
        {
            if (fEnabled)
            {
                if (fCallback)
                {
                    fCallback();
                }
                if (fRepeatInterval == INFINITE)
                {
                    fEnabled = false;
                }
            }
        }

        static LRESULT CALLBACK WindowProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
        {
            switch (msg)
            {
                case ON_TIMER_MESSAGE:
                    reinterpret_cast<Impl*>(wParam)->ExecuteTimerFuncThreadSafe();
                    return 0;
                default:
                    return DefWindowProc(hWnd, msg, wParam, lParam);
            }
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
            fWindowHandle = CreateWindowEx(0, CLASS_NAME, nullptr, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                           CW_USEDEFAULT, CW_USEDEFAULT, nullptr, nullptr, GetModuleHandle(nullptr),
                                           nullptr);
        }

        void UnregisterWindow()
        {
            if (fWindowHandle != nullptr)
            {
                DestroyWindow(fWindowHandle);
                fWindowHandle = nullptr;
            }
        }

        static constexpr LWS::char_type CLASS_NAME[] = L"LWS.HighPrecisionTimerWindow";
        static constexpr UINT ON_TIMER_MESSAGE = WM_USER + 1;
        static inline std::once_flag fCreateClassOnceFlag;
        bool fEnabled = false;
        HANDLE fTimerID = nullptr;
        Callback fCallback;
        uint32_t fDueTime = INFINITE;
        uint32_t fRepeatInterval = INFINITE;
        HWND fWindowHandle = nullptr;
    };

    HighPrecisionTimer::HighPrecisionTimer(Callback callback) : impl_(std::make_unique<Impl>(std::move(callback))) {}
    HighPrecisionTimer::~HighPrecisionTimer() = default;
    void HighPrecisionTimer::SetRepeatInterval(uint32_t repeatInterval)
    {
        impl_->SetRepeatInterval(repeatInterval);
    }
    void HighPrecisionTimer::SetDueTime(uint32_t dueTime)
    {
        impl_->SetDueTime(dueTime);
    }
    bool HighPrecisionTimer::GetEnabled() const
    {
        return impl_->GetEnabled();
    }
    void HighPrecisionTimer::Enable(bool enable)
    {
        impl_->Enable(enable);
    }
}  // namespace LWS

namespace
{
    void CALLBACK TimerManager::TimerProc(HWND, UINT, UINT_PTR idTimer, DWORD)
    {
        TimerManager& manager = TimerManager::GetSingleton();
        auto it = manager.fMapTimerIdToTimer.find(static_cast<TimerIDType>(idTimer));
        if (it == manager.fMapTimerIdToTimer.end())
        {
            LL_EXCEPTION(LLUtils::Exception::ErrorCode::InvalidState, "Timer id not found in data structure");
        }

        it->second->Execute();
    }
}  // namespace
#endif
