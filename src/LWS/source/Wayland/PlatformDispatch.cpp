#include "../internal/PlatformBackend.hpp"

#ifdef LWS_PLATFORM_WAYLAND
    #include "internal/PlatformState.hpp"
#endif

#include <tuple>

#if !defined(LWS_PLATFORM_WIN32)
namespace LWS::internal
{
    std::unique_ptr<PlatformBackend> CreatePlatformBackend(BackendId backend, [[maybe_unused]] PlatformContext& context)
    {
    #ifdef LWS_PLATFORM_WAYLAND
        if (backend == BackendId::Wayland)
            return std::make_unique<WaylandPlatformState>(context);
    #else
        std::ignore = backend;
    #endif
        return nullptr;
    }
}  // namespace LWS::internal
#endif
