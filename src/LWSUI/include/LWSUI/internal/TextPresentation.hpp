#pragma once
#include <LWSUI/Primitives.hpp>
#include <LWSUI/TextEditing.hpp>
namespace LWSUI::internal
{
    // Callers own padding, clipping and highlights. Coordinates stay in logical units.
    inline Rect AlignedTextBounds(Canvas& canvas, std::string_view text, Rect bounds, const FontSpec& font,
                                  Alignment horizontal, Alignment vertical, bool wrap = false,
                                  bool constrainHeight = true)
    {
        if (bounds.width <= 0 || bounds.height <= 0)
            return {bounds.x, bounds.y, 0, 0};
        const float measured = canvas.TextHeight(text, std::max(1.f, bounds.width), font, wrap);
        const float height = constrainHeight ? std::min(bounds.height, measured) : measured;
        const float room = bounds.height - height;
        bounds.y += vertical == Alignment::Center ? room / 2 : vertical == Alignment::End ? room : 0;
        if (!wrap && horizontal != Alignment::Start)
        {
            const float space = std::max(0.f, bounds.width - canvas.Measure(text, font));
            const float shift = horizontal == Alignment::Center ? space / 2 : space;
            bounds.x += shift;
            bounds.width -= shift;
        }
        bounds.height = height;
        return bounds;
    }
    inline std::string EllipsizeText(Canvas& canvas, std::string_view text, float width, const FontSpec& font)
    {
        width = std::max(0.f, width);
        if (canvas.Measure(text, font) <= width)
            return std::string(text);
        const std::string dots = "\xe2\x80\xa6";
        if (canvas.Measure(dots, font) > width)
            return {};
        std::vector<size_t> offsets{0};
        for (size_t at = 0; at < text.size();)
        {
            at = NextCharacter(text, at);
            offsets.push_back(at);
        }
        size_t low = 0, high = offsets.size();
        while (low + 1 < high)
        {
            const auto middle = (low + high) / 2;
            if (canvas.Measure(std::string(text.substr(0, offsets[middle])) + dots, font) <= width)
                low = middle;
            else
                high = middle;
        }
        return std::string(text.substr(0, offsets[low])) + dots;
    }
}  // namespace LWSUI::internal
