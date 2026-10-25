#ifdef LWS_PLATFORM_WIN32

    #include "internal/PlatformBackendWin32.hpp"
    #include "internal/SharedWindowClass.hpp"
    #include "internal/MessageCleanup.hpp"

namespace LWS::internal
{
    // An external host does not wait on LWS events. A context-owned message-only window lets its normal
    // UI loop deliver coalesced task wakeups on the context thread, including during native modal loops.
    // This has higher measured handoff latency than the standalone event path, so the frontend selects
    // it when initializing its context. No runtime mode branch or second task queue is needed. The host must destroy
    // all bound windows and services and shut down the context on its UI thread before unloading the module.
    class ExternalLoopBackend final : public Win32PlatformBackendCommon
    {
      public:

        explicit ExternalLoopBackend(PlatformContext& context) : context_(context) {}

        ~ExternalLoopBackend() override { Shutdown(); }

        Result Initialize() override
        {
            const Result result = platform_backend::init();
            if (result != Result::Success)
                return result;
            platformInitialized_ = true;
            classRegistered_ = taskClass_.Acquire();
            if (classRegistered_)
                taskWindow_ = CreateWindowExW(0, taskClass_.Name(), nullptr, 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr,
                                              taskClass_.Module(), this);
            if (taskWindow_ == nullptr)
            {
                Shutdown();
                return Result::Failure;
            }
            return Result::Success;
        }

        void Shutdown() override
        {
            // OLE cleanup can run a nested loop. Keep quit preservation alive until it finishes.
            const MessageCleanup cleanup(taskWindow_, TaskMessage);
            if (taskWindow_ != nullptr)
            {
                DestroyWindow(taskWindow_);
                taskWindow_ = nullptr;
            }
            if (classRegistered_)
            {
                taskClass_.Release();
                classRegistered_ = false;
            }
            if (platformInitialized_)
            {
                platform_backend::shutdown();
                platformInitialized_ = false;
            }
        }

        // Entering a second loop could consume the host's quit message or dispatch unrelated work recursively.
        LoopResult RunMessageLoop(PlatformContext&) override { return LoopResult::Failed; }
        LoopResult ProcessMessages(PlatformContext&) override { return LoopResult::Failed; }

        Result Wake() override
        {
            return PostMessageW(taskWindow_, TaskMessage, 0, 0) != FALSE ? Result::Success : Result::Failure;
        }
        // Retrieving the posted message consumes its wakeup. Do not pump messages here: DrainTasks
        // calls this under its queue mutex, and Win32 retrieval can dispatch sent messages recursively.
        void ClearWake() override {}

      private:

        // The host dispatches this window normally. LWS does not subclass the host or start a nested pump.
        static LRESULT CALLBACK TaskWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
        {
            if (message == WM_NCCREATE)
            {
                const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
                SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
            }
            else if (message == TaskMessage)
            {
                auto& context =
                    reinterpret_cast<ExternalLoopBackend*>(GetWindowLongPtrW(window, GWLP_USERDATA))->context_;
                if (context.IsUsable() && PlatformContextAccess::GetLoopResult(context) == LoopResult::Continue)
                {
                    // Host-driven dispatch needs the same shutdown/reentrancy protection as the LWS loop.
                    const PlatformContextAccess::DispatchScope dispatch(context);
                    PlatformContextAccess::DrainTasks(context);
                }
                return 0;
            }
            return DefWindowProcW(window, message, wParam, lParam);
        }

        static constexpr UINT TaskMessage = WM_APP + 1;
        PlatformContext& context_;
        HWND taskWindow_{};
        static inline SharedWindowClass taskClass_{L"LWS.TaskDispatcher", TaskWindowProc};
        bool classRegistered_{};
        bool platformInitialized_{};
    };

    std::unique_ptr<PlatformBackend> CreateExternalLoopBackend(PlatformContext& context)
    {
        return std::make_unique<ExternalLoopBackend>(context);
    }
}  // namespace LWS::internal

#endif
