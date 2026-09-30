#include <LWSUI/Canvas.hpp>
#include <LWSUI/TextEditing.hpp>
#include <LWS/Bitmap.hpp>
#include <algorithm>
#include <climits>
#include <stdexcept>
#include <string>
#include <map>
#include <numbers>
#include <tuple>
#include <pango/pangocairo.h>
#include <fontconfig/fontconfig.h>

namespace LWSUI
{
    struct Canvas::Impl
    {
        int width = 0, height = 0;
        Impl()
        {
            if (!FcInit())
                throw std::runtime_error("Cannot initialize system fonts");
            surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
            context = cairo_create(surface);
        }
        std::vector<std::byte> pixels;
        cairo_surface_t* surface = nullptr;
        cairo_t* context = nullptr;
        std::map<std::tuple<std::string, float, FontSpec, bool>, std::shared_ptr<PangoLayout>> layouts;
        ~Impl()
        {
            layouts.clear();
            if (context)
                cairo_destroy(context);
            if (surface)
                cairo_surface_destroy(surface);
        }
        PangoLayout* Layout(std::string_view text, float width, const FontSpec& font, bool wrap)
        {
            auto key = std::make_tuple(std::string(text), wrap ? width : 100000.f, font, wrap);
            if (auto found = layouts.find(key); found != layouts.end())
                return static_cast<PangoLayout*>(g_object_ref(found->second.get()));
            // Bound memory with simple eviction. An LRU needs extra ownership and
            // recency bookkeeping; defer it until measured cache churn warrants it.
            if (layouts.size() > 512)
                layouts.clear();
            PangoLayout* layout = pango_cairo_create_layout(context);
            pango_layout_set_text(layout, text.data(), int(text.size()));
            auto* description = pango_font_description_new();
            pango_font_description_set_family(description, font.family.empty() ? "sans-serif" : font.family.c_str());
            pango_font_description_set_absolute_size(description, font.size * PANGO_SCALE);
            pango_font_description_set_weight(description, PangoWeight(font.weight));
            pango_font_description_set_style(description, font.italic ? PANGO_STYLE_ITALIC : PANGO_STYLE_NORMAL);
            pango_layout_set_font_description(layout, description);
            pango_font_description_free(description);
            if (wrap)
            {
                pango_layout_set_width(layout, int(width * PANGO_SCALE));
                pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
            }
            layouts.emplace(std::move(key),
                            std::shared_ptr<PangoLayout>(static_cast<PangoLayout*>(g_object_ref(layout)),
                                                         [](PangoLayout* p) { g_object_unref(p); }));
            return layout;
        }
    };
    Canvas::Canvas() : impl_(std::make_unique<Impl>()) {}
    Canvas::~Canvas() = default;
    void Canvas::Begin(int width, int height, LWS::ContentScale scale)
    {
        auto& c = *impl_;
        width = std::max(1, width);
        height = std::max(1, height);
        if (width != c.width || height != c.height)
        {
            if (c.context)
                cairo_destroy(c.context);
            if (c.surface)
                cairo_surface_destroy(c.surface);
            c.pixels.resize(size_t(width) * size_t(height) * 4);
            c.surface = cairo_image_surface_create_for_data(reinterpret_cast<unsigned char*>(c.pixels.data()),
                                                            CAIRO_FORMAT_ARGB32, width, height, width * 4);
            c.context = cairo_create(c.surface);
            if (cairo_status(c.context) != CAIRO_STATUS_SUCCESS)
                throw std::runtime_error("Cannot create settings canvas");
        }
        cairo_identity_matrix(c.context);
        cairo_scale(c.context, scale.x, scale.y);
        c.width = width;
        c.height = height;
        scale_ = scale;
    }
    void Canvas::Fill(float x, float y, float width, float height, LLUtils::Color color)
    {
        if (width <= 0 || height <= 0)
            return;
        auto* c = impl_->context;
        cairo_set_source_rgba(c, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_rectangle(c, x, y, width, height);
        cairo_fill(c);
    }
    void Canvas::Text(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                      const FontSpec& font, bool wrap, std::span<const TextSpan> spans)
    {
        PaintText(text, x, y, width, height, color, font, wrap, spans, false);
    }
    void Canvas::CenteredText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                              const FontSpec& font, std::span<const TextSpan> spans)
    {
        PaintText(text, x, y, width, height, color, font, false, spans, true);
    }
    void Canvas::PaintText(std::string_view text, float x, float y, float width, float height, LLUtils::Color color,
                           const FontSpec& font, bool wrap, std::span<const TextSpan> spans, bool centered)
    {
        if (text.empty() || width <= 0 || height <= 0)
            return;
        ClipScope clip(*this, x, y, width, height);
        auto* layout = impl_->Layout(text, width, font, wrap);
        if (centered)
        {
            PangoRectangle ink{};
            pango_layout_get_extents(layout, &ink, nullptr);
            x += std::max(0.f, (width - float(ink.width) / PANGO_SCALE) / 2) - float(ink.x) / PANGO_SCALE;
            y += (height - float(ink.height) / PANGO_SCALE) / 2 - float(ink.y) / PANGO_SCALE;
        }
        auto* attributes = pango_attr_list_new();
        for (const auto& span : spans)
        {
            auto add = [&](PangoAttribute* attribute)
            {
                attribute->start_index = unsigned(span.offset);
                attribute->end_index = unsigned(span.offset + span.length);
                pango_attr_list_change(attributes, attribute);
            };
            auto f = span.foreground, b = span.background;
            add(pango_attr_foreground_new(f.R() * 257, f.G() * 257, f.B() * 257));
            add(pango_attr_foreground_alpha_new(f.A() * 257));
            add(pango_attr_background_new(b.R() * 257, b.G() * 257, b.B() * 257));
            add(pango_attr_background_alpha_new(b.A() * 257));
        }
        pango_layout_set_attributes(layout, attributes);
        pango_attr_list_unref(attributes);
        cairo_set_source_rgba(impl_->context, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_move_to(impl_->context, x, y);
        pango_cairo_show_layout(impl_->context, layout);
        pango_layout_set_attributes(layout, nullptr);
        g_object_unref(layout);
    }
    void Canvas::Line(float x1, float y1, float x2, float y2, float thickness, LLUtils::Color color)
    {
        if (thickness <= 0)
            return;
        auto* c = impl_->context;
        cairo_save(c);
        cairo_set_source_rgba(c, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_set_line_width(c, thickness);
        cairo_move_to(c, x1, y1);
        cairo_line_to(c, x2, y2);
        cairo_stroke(c);
        cairo_restore(c);
    }
    void Canvas::FillCircle(float centerX, float centerY, float radius, LLUtils::Color color)
    {
        if (radius <= 0)
            return;
        auto* c = impl_->context;
        cairo_set_source_rgba(c, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_new_path(c);
        cairo_arc(c, centerX, centerY, radius, 0, 2 * std::numbers::pi);
        cairo_fill(c);
    }
    void Canvas::StrokeCircle(float centerX, float centerY, float radius, float thickness, LLUtils::Color color)
    {
        if (radius <= 0 || thickness <= 0)
            return;
        auto* c = impl_->context;
        cairo_save(c);
        cairo_set_source_rgba(c, color.R() / 255., color.G() / 255., color.B() / 255., color.A() / 255.);
        cairo_set_line_width(c, thickness);
        cairo_new_path(c);
        cairo_arc(c, centerX, centerY, radius, 0, 2 * std::numbers::pi);
        cairo_close_path(c);
        cairo_stroke(c);
        cairo_restore(c);
    }
    void Canvas::Image(const LWS::BitmapBuffer& source, float x, float y, float width, float height)
    {
        if (width <= 0 || height <= 0)
            return;
        LWS::Bitmap normalized(source);
        const auto image = normalized.GetBuffer();
        auto* surface = cairo_image_surface_create_for_data(
            reinterpret_cast<unsigned char*>(const_cast<std::byte*>(image.pixels.data())), CAIRO_FORMAT_ARGB32,
            int(image.width), int(image.height), int(image.rowPitch));
        auto* c = impl_->context;
        cairo_save(c);
        cairo_translate(c, x, y);
        cairo_scale(c, width / image.width, height / image.height);
        cairo_set_source_surface(c, surface, 0, 0);
        cairo_rectangle(c, 0, 0, image.width, image.height);
        cairo_fill(c);
        cairo_restore(c);
        cairo_surface_destroy(surface);
    }
    float Canvas::Measure(std::string_view text, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, 100000, font, false);
        int width = 0;
        pango_layout_get_pixel_size(layout, &width, nullptr);
        g_object_unref(layout);
        return float(width);
    }
    size_t Canvas::HitTestText(std::string_view text, float x, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, 100000, font, false);
        int index = 0, trailing = 0;
        pango_layout_xy_to_index(layout, int(std::clamp(double(x) * PANGO_SCALE, double(INT_MIN), double(INT_MAX))), 0,
                                 &index, &trailing);
        g_object_unref(layout);
        size_t offset = std::min(size_t(std::max(0, index)), text.size());
        while (trailing-- > 0 && offset < text.size())
            offset = NextCharacter(text, offset);
        return offset;
    }
    float Canvas::Caret(std::string_view text, size_t byteOffset, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, 100000, font, false);
        PangoRectangle strong{}, weak{};
        pango_layout_get_cursor_pos(layout, int(byteOffset), &strong, &weak);
        g_object_unref(layout);
        return float(strong.x) / PANGO_SCALE;
    }
    std::vector<TextLine> Canvas::TextLines(std::string_view text, float width, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, std::max(1.f, width), font, true);
        auto* iterator = pango_layout_get_iter(layout);
        std::vector<TextLine> lines;
        do
        {
            const auto* line = pango_layout_iter_get_line_readonly(iterator);
            PangoRectangle bounds{};
            pango_layout_iter_get_line_extents(iterator, nullptr, &bounds);
            size_t length = size_t(line->length);
            while (length && (text[size_t(line->start_index) + length - 1] == '\n' ||
                              text[size_t(line->start_index) + length - 1] == '\r'))
                --length;
            lines.push_back({{size_t(line->start_index), length},
                             float(bounds.y) / PANGO_SCALE,
                             float(bounds.height) / PANGO_SCALE});
        } while (pango_layout_iter_next_line(iterator));
        pango_layout_iter_free(iterator);
        g_object_unref(layout);
        return lines;
    }
    TextPosition Canvas::HitTestText(std::string_view text, float x, float y, float width, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, std::max(1.f, width), font, true);
        const auto unit = [](float value)
        { return int(std::clamp(double(value) * PANGO_SCALE, double(INT_MIN), double(INT_MAX))); };
        int index = 0, trailing = 0;
        pango_layout_xy_to_index(layout, unit(x), unit(y), &index, &trailing);
        g_object_unref(layout);
        size_t offset = std::min(size_t(std::max(0, index)), text.size());
        const bool upstream = trailing > 0;
        while (trailing-- > 0 && offset < text.size())
            offset = NextCharacter(text, offset);
        return {offset, upstream && offset > 0 && text[offset - 1] != '\n'};
    }
    TextCaret Canvas::CaretBounds(std::string_view text, TextPosition position, float width, const FontSpec& font)
    {
        auto* layout = impl_->Layout(text, std::max(1.f, width), font, true);
        position.offset = std::min(position.offset, text.size());
        const bool trailing = position.upstream && position.offset && text[position.offset - 1] != '\n';
        const auto offset = trailing ? PreviousCharacter(text, position.offset) : position.offset;
        int line = 0, x = 0;
        pango_layout_index_to_line_x(layout, int(offset), trailing, &line, &x);
        auto* iterator = pango_layout_get_iter(layout);
        while (line-- > 0)
            pango_layout_iter_next_line(iterator);
        PangoRectangle bounds{};
        pango_layout_iter_get_line_extents(iterator, nullptr, &bounds);
        TextCaret caret{float(bounds.x + x) / PANGO_SCALE, float(bounds.y) / PANGO_SCALE, float(bounds.height) / PANGO_SCALE};
        pango_layout_iter_free(iterator);
        g_object_unref(layout);
        return caret;
    }
    float Canvas::TextHeight(std::string_view text, float width, const FontSpec& font, bool wrap)
    {
        auto* layout = impl_->Layout(text, width, font, wrap);
        int height = 0;
        pango_layout_get_pixel_size(layout, nullptr, &height);
        g_object_unref(layout);
        return float(height);
    }
    void Canvas::Clip(float x, float y, float width, float height)
    {
        cairo_save(impl_->context);
        cairo_rectangle(impl_->context, x, y, std::max(0.f, width), std::max(0.f, height));
        cairo_clip(impl_->context);
    }
    void Canvas::Unclip()
    {
        cairo_restore(impl_->context);
    }
    LWS::BitmapBuffer Canvas::End()
    {
        auto& c = *impl_;
        cairo_surface_flush(c.surface);
        return {c.pixels,
                LWS::BitmapPixelFormat::Bgra8Premultiplied,
                LWS::BitmapRowOrder::TopDown,
                uint32_t(c.width),
                uint32_t(c.height),
                uint32_t(c.width * 4)};
    }
}  // namespace LWSUI
