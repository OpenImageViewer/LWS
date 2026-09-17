#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>
    #include <LWS/Wayland/WindowExtensions.hpp>
    #include <LWS/Window.hpp>

    #include "../internal/WindowBackendAccess.hpp"

namespace LWS::Wayland
{
    namespace
    {
        WindowBackendWayland* GetBackend(Window& window)
        {
            std::ignore = window.GetPlatformContext();
            auto* backend = internal::WindowBackendAccess::Get(window);
            return backend != nullptr && backend->backend() == BackendId::Wayland
                       ? static_cast<WindowBackendWayland*>(backend)
                       : nullptr;
        }
    }  // namespace

    std::expected<wl_surface*, Result> GetSurface(const Window& window)
    {
        const bool available = internal::WindowBackendAccess::HasNativeHandle(window);
        const auto* backend = internal::WindowBackendAccess::Get(window);
        if (backend == nullptr || backend->backend() != BackendId::Wayland)
            return std::unexpected(Result::NotSupported);
        if (!available)
            return std::unexpected(Result::InvalidState);
        const auto* native = static_cast<const WindowBackendWayland*>(backend);
        return static_cast<wl_surface*>(native->surface());
    }

    std::expected<wl_display*, Result> GetDisplay(Window& window)
    {
        auto* backend = GetBackend(window);
        if (backend == nullptr)
            return std::unexpected(Result::NotSupported);
        if (!internal::WindowBackendAccess::HasNativeHandle(window))
            return std::unexpected(Result::InvalidState);
        return backend->display();
    }

    Result SetAppId(Window& window, std::string_view appId)
    {
        auto* backend = GetBackend(window);
        if (backend == nullptr)
            return Result::NotSupported;
        if (!internal::WindowBackendAccess::CanConfigure(window))
            return Result::InvalidState;
        if (appId.empty() || appId.contains('\0'))
            return Result::InvalidArgument;
        backend->setAppId(std::string(appId));
        return Result::Success;
    }
}  // namespace LWS::Wayland

#endif
