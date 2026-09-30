#pragma once
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>

namespace LWSUI::internal
{
    inline float MenuHintWidth(const Control& row, std::string_view hint, float width)
    {
        return hint.empty() ? 0
                            : std::min(std::max(0.f, width * .4f),
                                       row.Host()->MeasureText(hint, 10000, false, row.Font()).width);
    }
    inline void RenderMenuSeparator(const Control& row, Canvas& canvas)
    {
        const auto b = row.Bounds();
        const auto& style = row.Style();
        canvas.Fill(b.x + style.menuPadding, b.y + b.height / 2, std::max(0.f, b.width - 2 * style.menuPadding),
                    style.borderWidth, style.line);
    }
    inline void RenderMenuCheck(const Control& row, Canvas& canvas, LLUtils::Color color)
    {
        const auto b = row.Bounds();
        const float x = b.x + row.Style().menuPadding, y = b.y + b.height / 2, unit = row.Font().size;
        canvas.Line(x, y, x + unit * .3f, y + unit * .25f, row.Style().borderWidth, color);
        canvas.Line(x + unit * .3f, y + unit * .25f, x + unit * .75f, y - unit * .3f, row.Style().borderWidth, color);
    }
    template <class Selectable>
    std::optional<size_t> NextMenuRow(size_t count, size_t from, int step, Selectable selectable)
    {
        for (size_t n = 0; n < count; ++n)
        {
            from = (from + count + (step > 0 ? 1 : count - 1)) % count;
            if (selectable(from))
                return from;
        }
        return std::nullopt;
    }
    template <class Rows>
    bool SelectMenuRow(Control& owner, const Rows& rows, std::optional<size_t>& active, std::optional<size_t> next)
    {
        if (active == next)
            return false;
        if (active)
            rows[*active]->active = false;
        active = next;
        if (active)
            rows[*active]->active = true;
        owner.Invalidate();
        return true;
    }
    template <class Rows>
    void RevealMenuRow(ScrollView& owner, const Rows& rows, std::optional<size_t> active)
    {
        if (!active || owner.Bounds().height <= 0)
            return;
        // Both menu lists stack rows without spacing. Content coordinates remain valid when
        // wheel/drag input changes the offset before the next layout updates row rectangles.
        float top = 0;
        for (size_t row = 0; row < *active; ++row)
            top += rows[row]->DesiredSize().height;
        const float bottom = top + rows[*active]->DesiredSize().height;
        if (top < owner.Offset())
            owner.SetOffset(top);
        else if (bottom > owner.Offset() + owner.Bounds().height)
            owner.SetOffset(bottom - owner.Bounds().height);
    }
}  // namespace LWSUI::internal
