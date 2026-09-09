#include "CaptionRenderer.hpp"

#ifdef LWS_PLATFORM_WAYLAND

    #include <algorithm>
    #include <cmath>

    #include <fontconfig/fontconfig.h>
    #include <pango/pangocairo.h>

namespace LWS::internal
{
    void renderCaptionTitle(std::span<uint32_t> pixels, int32_t width, int32_t height, std::string_view title,
                            int32_t rightEdge, double scale)
    {
        const int32_t titleLeft = static_cast<int32_t>(std::lround(10.0 * scale));
        const int32_t fontSize = static_cast<int32_t>(std::lround(13.0 * scale));
        if (title.empty() || width <= titleLeft)
            return;

        static const bool fontConfigInitialized = FcInit();
        if (!fontConfigInitialized)
            return;

        const int32_t titleRight = std::clamp(rightEdge, titleLeft, width);
        const size_t requiredPixels = static_cast<size_t>(width) * height;
        if (pixels.size() < requiredPixels || titleRight == titleLeft)
            return;

        const int32_t stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
        cairo_surface_t* surface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(pixels.data()),
                                                                       CAIRO_FORMAT_ARGB32, width, height,
                                                                       stride);
        cairo_t* context = cairo_create(surface);
        cairo_rectangle(context, titleLeft, 0, titleRight - titleLeft, height);
        cairo_clip(context);
        cairo_set_source_rgb(context, 1.0, 1.0, 1.0);

        PangoLayout* layout = pango_cairo_create_layout(context);
        pango_layout_set_text(layout, title.data(), static_cast<int>(title.size()));
        pango_layout_set_width(layout, (titleRight - titleLeft) * PANGO_SCALE);
        pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_END);
        pango_layout_set_single_paragraph_mode(layout, true);
        PangoFontDescription* font = pango_font_description_from_string("sans-serif");
        pango_font_description_set_size(font, fontSize * PANGO_SCALE);
        pango_layout_set_font_description(layout, font);

        PangoRectangle logicalBounds{};
        pango_layout_get_pixel_extents(layout, nullptr, &logicalBounds);
        cairo_move_to(context, titleLeft, (height - logicalBounds.height) / 2.0 - logicalBounds.y);
        pango_cairo_show_layout(context, layout);
        cairo_surface_flush(surface);

        pango_font_description_free(font);
        g_object_unref(layout);
        cairo_destroy(context);
        cairo_surface_destroy(surface);
    }
}  // namespace LWS::internal

#endif
