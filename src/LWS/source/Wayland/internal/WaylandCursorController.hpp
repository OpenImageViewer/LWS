#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/interfaces/backends.hpp>

    #include <cstdint>
    #include <memory>

struct wl_compositor;
struct wl_pointer;
struct wl_shm;

namespace LWS::internal
{
    class WaylandCursorController
    {
      public:

        WaylandCursorController();
        ~WaylandCursorController();

        void apply(CursorShape shape, bool visible, wl_pointer* pointer, uint32_t enterSerial,
                   wl_compositor* compositor, wl_shm* sharedMemory, int32_t scale);
        void reset();

      private:

        class NativeState;
        std::unique_ptr<NativeState> fNativeState;
    };
}  // namespace LWS::internal

#endif
