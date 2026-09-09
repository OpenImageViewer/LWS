#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <LWS/Platform.hpp>

    #include <cstdint>
    #include <vector>

    #include <wayland-client.h>

namespace LWS::internal
{
    class WaylandPlatformState;

    class WaylandOutputManager
    {
      public:

        explicit WaylandOutputManager(WaylandPlatformState& platform) : fPlatform(platform) {}

        void bindOutput(wl_registry* registry, uint32_t name, uint32_t version);
        void removeGlobal(uint32_t name);
        [[nodiscard]] MonitorDesc monitorInfo(Handle handle) const;
        [[nodiscard]] MonitorDesc primaryMonitor() const;
        [[nodiscard]] Rect boundingMonitorArea() const;
        [[nodiscard]] int32_t scale(wl_output* output) const;
        void reset();

      private:

        struct Output
        {
            uint32_t registryName = 0;
            wl_output* object = nullptr;
            MonitorDesc description;
            int32_t scale = 1;
        };

        static void outputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth,
                                   int32_t physicalHeight, int32_t subpixel, const char* make, const char* model,
                                   int32_t transform);
        static void outputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height,
                               int32_t refresh);
        static void outputDone(void* data, wl_output* output);
        static void outputScale(void* data, wl_output* output, int32_t factor);
        static void outputName(void* data, wl_output* output, const char* name);
        static void outputDescription(void* data, wl_output* output, const char* description);

        [[nodiscard]] Output* findOutput(wl_output* output);

        WaylandPlatformState& fPlatform;
        std::vector<Output> fOutputs;
    };
}  // namespace LWS::internal

#endif
