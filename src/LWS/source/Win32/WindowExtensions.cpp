#ifdef LWS_PLATFORM_WIN32

    #include <LWS/Win32/WindowBackendWin32.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>

    #include "../internal/WindowBackendAccess.hpp"

    #include <utility>

namespace LWS::Win32
{
    namespace
    {
        WindowBackendWin32* GetBackend(Window& window)
        {
            window.GetPlatformContext().AssertCurrentThread();
            internal::IWindowBackend* backend = internal::WindowBackendAccess::Get(window);
            if (backend == nullptr || backend->backend() != BackendId::Win32)
                return nullptr;

            // Built-in BackendId values identify their concrete backend type.
            return static_cast<WindowBackendWin32*>(backend);
        }
    }  // namespace

    std::expected<HWND, Result> GetHwnd(Window& window)
    {
        WindowBackendWin32* backend = GetBackend(window);
        if (backend == nullptr)
            return std::unexpected(Result::NotSupported);
        if (!window.IsCreated())
            return std::unexpected(Result::InvalidState);
        return reinterpret_cast<HWND>(backend->getHandle());
    }

    std::expected<HWND, Result> GetHwnd(const Window& window)
    {
        return GetHwnd(const_cast<Window&>(window));
    }

    Result SetPlatformCallback(Window& window, PlatformCallback callback)
    {
        return internal::WindowBackendAccess::SetPlatformCallback(window, std::move(callback));
    }

    Result SetMenuChar(Window& window, bool suppress)
    {
        WindowBackendWin32* backend = GetBackend(window);
        if (backend == nullptr)
            return Result::NotSupported;

        backend->setMenuChar(suppress);
        return Result::Success;
    }
}  // namespace LWS::Win32

#endif  // LWS_PLATFORM_WIN32
