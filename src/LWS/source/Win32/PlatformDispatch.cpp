#ifdef LWS_PLATFORM_WIN32

    #include "internal/PlatformBackendWin32.hpp"
    #include <tuple>

namespace LWS::internal
{
    // Standalone applications own their message loop, so a waitable event can wake it directly without
    // routing task delivery through the Windows message queue. This preserves the original LWS scheduling
    // path and its lower handoff latency. It requires RunMessageLoop or ProcessMessages to service tasks.
    // External host loops use the message-window dispatcher selected by the factory during initialization.
    class OwnedLoopBackend final : public Win32PlatformBackendCommon
    {
      public:

        ~OwnedLoopBackend() override { Shutdown(); }

        Result Initialize() override
        {
            const Result result = platform_backend::init();
            if (result != Result::Success)
                return result;
            taskEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (taskEvent_ == nullptr)
            {
                platform_backend::shutdown();
                return Result::Failure;
            }
            return Result::Success;
        }

        void Shutdown() override
        {
            if (taskEvent_ != nullptr)
            {
                CloseHandle(taskEvent_);
                taskEvent_ = nullptr;
                platform_backend::shutdown();
            }
        }

        LoopResult RunMessageLoop(PlatformContext& context) override
        {
            while (!PlatformContextAccess::QuitRequested(context))
            {
                const DWORD result = MsgWaitForMultipleObjectsEx(1, &taskEvent_, INFINITE, QS_ALLINPUT,
                                                                 MWMO_INPUTAVAILABLE);
                if (result == WAIT_OBJECT_0)
                    PlatformContextAccess::DrainTasks(context);
                if (result == WAIT_OBJECT_0 || result == WAIT_OBJECT_0 + 1)
                    DispatchMessages(context);
                else if (result == WAIT_FAILED)
                    PlatformContextAccess::Fail(context, static_cast<int>(GetLastError()),
                                                "MsgWaitForMultipleObjectsEx");
            }
            return PlatformContextAccess::GetLoopResult(context);
        }

        LoopResult ProcessMessages(PlatformContext& context) override
        {
            const DWORD wait = WaitForSingleObject(taskEvent_, 0);
            if (wait == WAIT_OBJECT_0)
                PlatformContextAccess::DrainTasks(context);
            else if (wait == WAIT_FAILED)
                PlatformContextAccess::Fail(context, static_cast<int>(GetLastError()), "WaitForSingleObject");
            DispatchMessages(context);
            return PlatformContextAccess::GetLoopResult(context);
        }

        Result Wake() override { return SetEvent(taskEvent_) != FALSE ? Result::Success : Result::Failure; }
        void ClearWake() override { std::ignore = ResetEvent(taskEvent_); }

      private:

        static void DispatchMessages(PlatformContext& context)
        {
            MSG message{};
            while (context.IsUsable() && PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE) != FALSE)
            {
                if (message.message == WM_QUIT)
                    context.RequestQuit();
                else
                {
                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
            }
        }

        HANDLE taskEvent_{};
    };

    std::unique_ptr<PlatformBackend> CreateExternalLoopBackend(PlatformContext& context);

    std::unique_ptr<PlatformBackend> CreatePlatformBackend(const PlatformConfig& config, PlatformContext& context)
    {
        if (config.backend != BackendId::Win32)
            return nullptr;
        return config.dispatchMode == DispatchMode::ExternalLoop ? CreateExternalLoopBackend(context)
                                                                 : std::make_unique<OwnedLoopBackend>();
    }
}  // namespace LWS::internal

#endif
