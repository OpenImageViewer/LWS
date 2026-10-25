#pragma once

#ifdef LWS_PLATFORM_WAYLAND

    #include <cstdint>
    #include <span>
    #include <string_view>

namespace LWS::internal
{
    // Render only button pixels. Callers retain caption fill, title clipping, and native submission order.
    void renderCaptionCloseButton(std::span<uint32_t> pixels, int32_t width, int32_t height, int32_t closeLeft,
                                  double scale);
    void renderCaptionTitle(std::span<uint32_t> pixels, int32_t width, int32_t height, std::string_view title,
                            int32_t rightEdge, double scale);
}  // namespace LWS::internal

#endif
