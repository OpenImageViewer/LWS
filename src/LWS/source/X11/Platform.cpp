#ifdef LWS_PLATFORM_X11

    #include "../internal/PlatformBackend.hpp"

namespace LWS::internal
{
    std::unique_ptr<PlatformBackend> CreatePlatformBackend(const PlatformConfig&, PlatformContext&)
    {
        return nullptr;
    }
}  // namespace LWS::internal

#endif
