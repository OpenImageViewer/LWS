#include "../internal/PlatformBackend.hpp"

#ifdef LWS_PLATFORM_WAYLAND
    #include "internal/PlatformState.hpp"
#endif

#include <tuple>

#if !defined(LWS_PLATFORM_WIN32)
namespace LWS::internal
{
    std::unique_ptr<PlatformBackend> CreatePlatformBackend(const PlatformConfig& config,
                                                           [[maybe_unused]] PlatformContext& context)
    {
    #ifdef LWS_PLATFORM_WAYLAND
        if (config.backend == BackendId::Wayland && config.dispatchMode == DispatchMode::OwnedLoop)
            return std::make_unique<WaylandPlatformState>(context);
    #else
        std::ignore = config;
    #endif
        return nullptr;
    }
}  // namespace LWS::internal
#endif
