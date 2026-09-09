#ifdef LWS_PLATFORM_WAYLAND

    #include "WaylandOutputManager.hpp"
    #include "PlatformState.hpp"

    #include <algorithm>
    #include <ranges>
    #include <utility>

namespace LWS::internal
{
    void WaylandOutputManager::bindOutput(wl_registry* registry, uint32_t name, uint32_t version)
    {
        Output output;
        output.registryName = name;
        output.object = static_cast<wl_output*>(
            wl_registry_bind(registry, name, &wl_output_interface, std::min(version, 4U)));
        output.description.handle = reinterpret_cast<Handle>(output.object);
        output.description.primary = fOutputs.empty();
        fOutputs.push_back(std::move(output));
        static constexpr wl_output_listener outputListener{
            .geometry = outputGeometry,
            .mode = outputMode,
            .done = outputDone,
            .scale = outputScale,
            .name = outputName,
            .description = outputDescription,
        };
        wl_output_add_listener(fOutputs.back().object, &outputListener, this);
    }

    void WaylandOutputManager::removeGlobal(uint32_t name)
    {
        const auto it = std::ranges::find(fOutputs, name, &Output::registryName);
        if (it == fOutputs.end())
            return;
        wl_output* output = it->object;
        fOutputs.erase(it);
        if (!fOutputs.empty())
            fOutputs.front().description.primary = true;
        fPlatform.outputChanged(output, true);
        wl_output_destroy(output);
    }

    MonitorDesc WaylandOutputManager::monitorInfo(Handle handle) const
    {
        const auto it = std::ranges::find_if(fOutputs, [handle](const Output& output)
                                             { return output.description.handle == handle; });
        return it != fOutputs.end() ? it->description : MonitorDesc{};
    }

    MonitorDesc WaylandOutputManager::primaryMonitor() const
    {
        return fOutputs.empty() ? MonitorDesc{} : fOutputs.front().description;
    }

    Rect WaylandOutputManager::boundingMonitorArea() const
    {
        if (fOutputs.empty())
            return {};

        Point topLeft = fOutputs.front().description.monitorRect.GetCorner(LLUtils::TopLeft);
        Point bottomRight = fOutputs.front().description.monitorRect.GetCorner(LLUtils::BottomRight);
        for (const Output& output : fOutputs | std::views::drop(1))
        {
            const Point outputTopLeft = output.description.monitorRect.GetCorner(LLUtils::TopLeft);
            const Point outputBottomRight = output.description.monitorRect.GetCorner(LLUtils::BottomRight);
            topLeft.x = std::min(topLeft.x, outputTopLeft.x);
            topLeft.y = std::min(topLeft.y, outputTopLeft.y);
            bottomRight.x = std::max(bottomRight.x, outputBottomRight.x);
            bottomRight.y = std::max(bottomRight.y, outputBottomRight.y);
        }
        return {topLeft, bottomRight};
    }

    int32_t WaylandOutputManager::scale(wl_output* output) const
    {
        const auto it = std::ranges::find(fOutputs, output, &Output::object);
        return it != fOutputs.end() ? it->scale : 1;
    }

    WaylandOutputManager::Output* WaylandOutputManager::findOutput(wl_output* output)
    {
        const auto it = std::ranges::find(fOutputs, output, &Output::object);
        return it != fOutputs.end() ? &*it : nullptr;
    }

    void WaylandOutputManager::outputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t, int32_t,
                                              int32_t, const char*, const char*, int32_t)
    {
        auto& manager = *static_cast<WaylandOutputManager*>(data);
        if (Output* item = manager.findOutput(output); item != nullptr)
        {
            const Size size{item->description.monitorRect.GetWidth(), item->description.monitorRect.GetHeight()};
            item->description.monitorRect = {{x, y}, {x + size.x, y + size.y}};
            item->description.workRect = item->description.monitorRect;
        }
    }

    void WaylandOutputManager::outputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height,
                                          int32_t refresh)
    {
        auto& manager = *static_cast<WaylandOutputManager*>(data);
        if ((flags & WL_OUTPUT_MODE_CURRENT) != 0)
        {
            if (Output* item = manager.findOutput(output); item != nullptr)
            {
                item->description.pixelSize = {width, height};
                const Point position = item->description.monitorRect.GetCorner(LLUtils::TopLeft);
                const int32_t logicalWidth = width / item->scale;
                const int32_t logicalHeight = height / item->scale;
                item->description.monitorRect = {position, {position.x + logicalWidth, position.y + logicalHeight}};
                item->description.workRect = item->description.monitorRect;
                item->description.displayFrequency = refresh > 0 ? static_cast<uint32_t>(refresh / 1000) : 0;
            }
        }
    }

    void WaylandOutputManager::outputDone(void*, wl_output*) {}

    void WaylandOutputManager::outputScale(void* data, wl_output* output, int32_t factor)
    {
        auto& manager = *static_cast<WaylandOutputManager*>(data);
        if (Output* item = manager.findOutput(output); item != nullptr)
        {
            item->scale = std::max(factor, 1);
            item->description.contentScale = {static_cast<double>(item->scale), static_cast<double>(item->scale)};
            if (item->description.pixelSize.x > 0 && item->description.pixelSize.y > 0)
            {
                const Point position = item->description.monitorRect.GetCorner(LLUtils::TopLeft);
                item->description.monitorRect = {
                    position,
                    {position.x + item->description.pixelSize.x / item->scale,
                     position.y + item->description.pixelSize.y / item->scale},
                };
                item->description.workRect = item->description.monitorRect;
            }
            manager.fPlatform.outputChanged(output, false);
        }
    }

    void WaylandOutputManager::outputName(void* data, wl_output* output, const char* name)
    {
        auto& manager = *static_cast<WaylandOutputManager*>(data);
        if (Output* item = manager.findOutput(output); item != nullptr && name != nullptr)
            item->description.deviceName = name;
    }

    void WaylandOutputManager::outputDescription(void*, wl_output*, const char*) {}

    void WaylandOutputManager::reset()
    {
        for (Output& output : fOutputs)
            wl_output_destroy(output.object);
        fOutputs.clear();
    }
}  // namespace LWS::internal

#endif
