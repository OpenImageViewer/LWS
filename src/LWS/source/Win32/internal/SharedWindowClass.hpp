#pragma once
#include <Windows.h>
#include <mutex>

namespace LWS::internal
{
    // One module-owned class per native window kind. References span native creation through destruction,
    // including failed creation, so the final release removes every procedure address before DLL unload.
    class SharedWindowClass final
    {
      public:

        SharedWindowClass(const wchar_t* name, WNDPROC procedure, UINT style = 0, HCURSOR cursor = nullptr)
            : definition_{.cbSize = sizeof(WNDCLASSEXW),
                          .style = style,
                          .lpfnWndProc = procedure,
                          .hCursor = cursor,
                          .lpszClassName = name}
        {
        }
        bool Acquire()
        {
            const std::scoped_lock lock(mutex_);
            if (users_ == 0)
            {
                if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                        reinterpret_cast<LPCWSTR>(definition_.lpfnWndProc), &definition_.hInstance) ||
                    RegisterClassExW(&definition_) == 0)
                    return false;
            }
            ++users_;
            return true;
        }
        void Release()
        {
            const std::scoped_lock lock(mutex_);
            if (--users_ == 0)
                UnregisterClassW(definition_.lpszClassName, definition_.hInstance);
        }
        HINSTANCE Module() const { return definition_.hInstance; }
        const wchar_t* Name() const { return definition_.lpszClassName; }

      private:

        WNDCLASSEXW definition_;
        std::mutex mutex_;
        size_t users_{};
    };
}  // namespace LWS::internal
