#ifdef LWS_PLATFORM_WIN32

    #include <LWS/source/Win32/internal/WindowBackendWin32.hpp>
    #include <LWS/Win32/WindowExtensions.hpp>

    #include "../internal/WindowBackendAccess.hpp"

    #include <utility>

namespace LWS::Win32
{
    std::expected<HWND, Result> GetHwnd(const Window& window)
    {
        // HasNativeHandle enforces affinity and the native lifetime boundary.
        const bool available = internal::WindowBackendAccess::HasNativeHandle(window);
        const auto* backend = internal::WindowBackendAccess::Get(window);
        if (backend == nullptr || backend->backend() != BackendId::Win32)
            return std::unexpected(Result::NotSupported);
        if (!available)
            return std::unexpected(Result::InvalidState);
        return reinterpret_cast<HWND>(backend->getHandle());
    }

    std::expected<EventConnection, Result> Listen(Window& window, PlatformCallback callback)
    {
        return internal::WindowBackendAccess::ListenPlatform(window, std::move(callback));
    }

    Result SetMouseCapture(Window& window, bool capture)
    {
        const auto handle = GetHwnd(window);
        if (!handle)
            return handle.error();
        if (!window.IsCreated())
            return Result::InvalidState;
        const internal::WindowBackendAccess::DispatchScope ownerDispatch(window);
        const internal::PlatformContextAccess::DispatchScope contextDispatch(window.GetPlatformContext());
        if (capture)
        {
            ::SetCapture(*handle);
            // The previous capture owner's notification can destroy this native window.
            if (!window.IsCreated())
                return Result::InvalidState;
            return ::GetCapture() == *handle ? Result::Success : Result::Failure;
        }
        if (::GetCapture() == *handle && !::ReleaseCapture())
            return Result::Failure;
        return Result::Success;
    }

    Result SetMenuChar(Window& window, bool suppress)
    {
        const bool configurable = internal::WindowBackendAccess::CanConfigure(window);
        auto* backend = internal::WindowBackendAccess::Get(window);
        if (backend == nullptr || backend->backend() != BackendId::Win32)
            return Result::NotSupported;
        if (!configurable)
            return Result::InvalidState;
        static_cast<WindowBackendWin32*>(backend)->setMenuChar(suppress);
        return Result::Success;
    }
}  // namespace LWS::Win32

#endif  // LWS_PLATFORM_WIN32
