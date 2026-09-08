#ifdef LWS_PLATFORM_X11

    #include "../internal/PlatformBackend.hpp"

namespace LWS::internal
{
    std::unique_ptr<PlatformBackend> CreatePlatformBackend(BackendId)
    {
        return nullptr;
    }
}  // namespace LWS::internal

#endif
