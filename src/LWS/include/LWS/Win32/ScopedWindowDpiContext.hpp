#pragma once

#ifdef _WIN32
    #include <Windows.h>

namespace LWS::Win32
{
    // Create related windows in the reference window's DPI context without changing process policy.
    // The scope stays on one thread and restores its previous context even when creation fails.
    // Resolve dynamically so applications remain loadable on Windows versions without these APIs.
    class ScopedWindowDpiContext final
    {
      public:

        explicit ScopedWindowDpiContext(HWND reference) noexcept
        {
            if (reference != nullptr)
            {
                using GetContext = HANDLE(WINAPI*)(HWND);
                static const auto user = GetModuleHandleW(L"user32.dll");
                static const auto get = reinterpret_cast<GetContext>(
                    GetProcAddress(user, "GetWindowDpiAwarenessContext"));
                static const auto set = reinterpret_cast<SetContext>(
                    GetProcAddress(user, "SetThreadDpiAwarenessContext"));
                fSet = set;
                if (get != nullptr && fSet != nullptr)
                    fPrevious = fSet(get(reference));
            }
        }
        ~ScopedWindowDpiContext() noexcept
        {
            if (fPrevious != nullptr)
                fSet(fPrevious);
        }
        ScopedWindowDpiContext(const ScopedWindowDpiContext&) = delete;
        ScopedWindowDpiContext& operator=(const ScopedWindowDpiContext&) = delete;

      private:

        using SetContext = HANDLE(WINAPI*)(HANDLE);
        SetContext fSet{};
        HANDLE fPrevious{};
    };
}  // namespace LWS::Win32
#endif
