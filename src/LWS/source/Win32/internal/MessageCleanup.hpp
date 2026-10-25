#pragma once
#include <Windows.h>
#include <optional>

namespace LWS::internal
{
    // PeekMessage can return WM_QUIT regardless of its filters. Hold it through all nested cleanup,
    // then restore the host's original exit code after purging only this window's private messages.
    class MessageCleanup final
    {
      public:

        explicit MessageCleanup(HWND window, UINT message)
        {
            if (window == nullptr)
                return;
            MSG pending{};
            while (PeekMessageW(&pending, window, message, message, PM_REMOVE))
            {
                if (pending.message == WM_QUIT)
                {
                    quitCode_ = static_cast<int>(pending.wParam);
                }
            }
        }
        ~MessageCleanup()
        {
            if (quitCode_)
                PostQuitMessage(*quitCode_);
        }
        MessageCleanup(const MessageCleanup&) = delete;
        MessageCleanup& operator=(const MessageCleanup&) = delete;

      private:

        std::optional<int> quitCode_;
    };
}  // namespace LWS::internal
