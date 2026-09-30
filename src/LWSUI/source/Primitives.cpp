#include <LWSUI/Primitives.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWSUI/TextEditing.hpp>
#include <LWS/TextClipboard.hpp>
#include <algorithm>
#include <cmath>
namespace LWSUI
{
    void Label::OnRender(Canvas& c)
    {
        auto b = Bounds();
        const float p = Style().labelPadding;
        const float width = std::max(0.f, b.width - 2 * p);
        std::string text = text_;
        if (trimming == TextTrimming::Ellipsis && !wrap && c.Measure(text, Font()) > width)
        {
            const std::string dots = "\xe2\x80\xa6";
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
                if (c.Measure(text.substr(0, offsets[middle]) + dots, Font()) <= width)
                    low = middle;
                else
                    high = middle;
            }
            text = c.Measure(dots, Font()) <= width ? text.substr(0, offsets[low]) + dots : "";
        }
        const float height = std::min(std::max(0.f, b.height - 2 * p),
                                      c.TextHeight(text, std::max(1.f, width), Font(), wrap));
        const float room = std::max(0.f, b.height - 2 * p - height);
        const float y = b.y + p +
                        (verticalAlignment == Alignment::Center ? room / 2
                         : verticalAlignment == Alignment::End  ? room
                                                                : 0);
        DrawText(c, text, b.x + p, y, width, height, Foreground(), wrap);
    }
    Size Label::OnMeasure(Size available)
    {
        const float p = Style().labelPadding;
        const auto& font = Font();
        float height = font.size * 1.4f;
        if (auto* host = Host())
        {
            const float contentWidth = std::max(1.f, available.width - 2 * p);
            const float keyWidth = wrap ? contentWidth : 0.f;
            const auto scale = host->Window().GetClientAreaMetrics().Scale();
            if (!measurement_ || measurement_->font != font || measurement_->scale != scale ||
                measurement_->wrap != wrap || measurement_->width != keyWidth)
            {
                // Publish only a successful native measurement. Detached fallback
                // estimates must never survive first attachment to a host.
                const float measured = host->MeasureText(text_, contentWidth, wrap, font).height;
                measurement_ = Measurement{font, scale, keyWidth, wrap, measured};
            }
            height = measurement_->height;
        }
        const float width = sizeToContent && !wrap ? (Host() ? Host()->MeasureText(text_, 100000, false, font).width
                                                             : text_.size() * font.size * .6f) +
                                                         2 * p
                                                   : available.width;
        return {width, std::max(Style().rowHeight, height + 2 * p)};
    }
    Size Label::OnMeasureContent(Size available)
    {
        auto size = OnMeasure(available);
        const auto width = [&](std::string_view value)
        { return Host() ? Host()->MeasureText(value, 100000, false, Font()).width : value.size() * Font().size * .6f; };
        size.width = std::max(width(text_), width(minimumText)) + 2 * Style().labelPadding;
        return size;
    }
    Size Button::OnMeasureContent(Size available)
    {
        auto size = Control::OnMeasure(available);
        size.width = (Host() ? Host()->MeasureText(text_, 100000, false, Font()).width
                             : text_.size() * Font().size * .6f) +
                     2 * Style().padding;
        return size;
    }
    Size Button::OnMeasure(Size available)
    {
        auto size = Control::OnMeasure(available);
        if (sizeToContent)
            size.width = (Host() ? Host()->MeasureText(text_, 100000, false, Font()).width
                                 : text_.size() * Font().size * .6f) +
                         2 * Style().padding;
        return size;
    }
    void Button::RenderBackground(Canvas& c) const
    {
        auto b = Bounds();
        const bool enabled = Enabled();
        if (!flat || (enabled && (pressed_ || Hovered())))
            c.Fill(b.x, b.y, b.width, b.height,
                   !enabled    ? Style().surface
                   : pressed_  ? Style().selectedRow
                   : Hovered() ? Style().hoverSurface
                               : Style().line);
    }
    void Button::OnRender(Canvas& c)
    {
        RenderBackground(c);
        const auto b = Bounds();
        const bool enabled = Enabled();
        const float inset = flat ? 0.f : Style().padding;
        c.CenteredText(text_, b.x + inset, b.y, b.width - 2 * inset, b.height, enabled ? Foreground() : Style().muted,
                       Font(), enabled ? HighlightSpans(text_) : std::vector<TextSpan>{});
        if (enabled && Host() && Host()->IsFocused(*this))
            c.Fill(b.x, b.y + b.height - Style().focusWidth, b.width, Style().focusWidth, Style().accent);
    }
    Size GlyphButton::OnMeasure(Size available)
    {
        const auto height = Control::OnMeasure(available).height;
        return {height, height};
    }
    void GlyphButton::OnRender(Canvas& canvas)
    {
        RenderBackground(canvas);
        const auto b = Bounds();
        const auto color = Enabled() ? Foreground() : Style().muted;
        const float side = std::min(b.width, b.height) * .45f, left = b.x + (b.width - side) / 2;
        const bool up = glyph_ == Glyph::ChevronUp || glyph_ == Glyph::DoubleChevronUp;
        const bool twice = glyph_ == Glyph::DoubleChevronUp || glyph_ == Glyph::DoubleChevronDown;
        for (int i = 0; i < (twice ? 2 : 1); ++i)
        {
            const float y = b.y + b.height / 2 + (twice ? (i ? .23f : -.23f) * side : 0);
            const float direction = up ? -1.f : 1.f;
            canvas.Line(left, y - direction * .18f * side, left + side / 2, y + direction * .18f * side,
                        std::max(1.f, Style().borderWidth), color);
            canvas.Line(left + side / 2, y + direction * .18f * side, left + side, y - direction * .18f * side,
                        std::max(1.f, Style().borderWidth), color);
        }
    }
    void Image::OnRender(Canvas& canvas)
    {
        if (!bitmap_)
            return;
        const auto b = Bounds();
        const auto pixels = bitmap_->GetBuffer();
        if (!pixels.width || !pixels.height)
            return;
        const float scale = std::min(b.width / pixels.width, b.height / pixels.height);
        const float width = pixels.width * scale, height = pixels.height * scale;
        canvas.Image(pixels, b.x + (b.width - width) / 2, b.y + (b.height - height) / 2, width, height);
    }
    bool Button::Finish(EditPhase phase)
    {
        if (pressed_)
        {
            pressed_ = false;
            ReleaseCapture();
            OnRelease.Raise(phase);
            OnPress.Raise(false);
            Invalidate();
        }
        return true;
    }
    bool Button::OnInput(const Input& e)
    {
        if (e.kind == InputKind::Move && pressed_)
        {
            pointerInside_ = Bounds().Contains(e.x, e.y);
            return false;
        }
        if (e.kind == InputKind::Down)
        {
            pointerInside_ = true;
            pressed_ = true;
            Capture();
            OnPress.Raise(true);
            Invalidate();
            return true;
        }
        if (e.kind == InputKind::Up && pressed_)
        {
            bool activate = Bounds().Contains(e.x, e.y);
            Finish(EditPhase::Commit);
            if (activate)
                Activate();
            return true;
        }
        if (e.kind == InputKind::Cancel)
            return Finish(EditPhase::Cancel);
        if (e.kind == InputKind::KeyDown && (e.key == LWS::KeyCode::Enter || e.key == LWS::KeyCode::Space))
        {
            if (!e.repeat)
                Activate();
            return true;
        }
        return false;
    }
    void CheckBox::Activate()
    {
        SetValue(!value_);
        OnChange.Raise(value_);
    }
    void CheckBox::OnRender(Canvas& canvas)
    {
        auto b = Bounds();
        const float size = Style().checkboxSize, top = b.y + (b.height - size) / 2;
        canvas.Fill(b.x, top, size, size, Value() ? Style().accent : Style().line);
        if (Value())
        {
            const float stroke = size * .10f;
            canvas.Line(b.x + size * .20f, top + size * .48f, b.x + size * .42f, top + size * .72f, stroke,
                        Style().surface);
            canvas.Line(b.x + size * .42f, top + size * .72f, b.x + size * .80f, top + size * .28f, stroke,
                        Style().surface);
        }
        const float inset = size + Style().choiceGap;
        const auto text = text_.empty() ? (Value() ? "Enabled" : "Disabled") : text_;
        const float height = canvas.TextHeight(text, b.width - inset, Font(), false);
        DrawText(canvas, text, b.x + inset, b.y + (b.height - height) / 2, b.width - inset, height, Foreground());
    }
    void RadioButton::OnRender(Canvas& canvas)
    {
        const auto b = Bounds();
        const float slot = std::max(0.f, std::min(Style().checkboxSize, b.width));
        const float size = std::max(0.f, std::min(slot, b.height));
        const float centerX = b.x + slot / 2, centerY = b.y + b.height / 2;
        const float stroke = std::min(Style().borderWidth, size / 8);
        const auto color = Enabled() && Value() ? Style().accent : Style().muted;
        canvas.StrokeCircle(centerX, centerY, (size - stroke) / 2, stroke, color);
        if (Value())
            canvas.FillCircle(centerX, centerY, size / 4, color);
        const float inset = Style().checkboxSize + Style().choiceGap / 2;
        const float height = canvas.TextHeight(text_, std::max(0.f, b.width - inset), Font(), false);
        DrawText(canvas, text_, b.x + inset, b.y + (b.height - height) / 2, b.width - inset, height,
                 Enabled() ? Foreground() : Style().muted);
    }
    Rect ScrollBar::Thumb() const
    {
        auto bounds = Bounds();
        float height = std::min(bounds.height, std::max(std::min(Style().scrollbarMinThumb, bounds.height / 2),
                                                        bounds.height * float(page_)));
        return {bounds.x + Style().scrollbarInset, bounds.y + float(value_) * (bounds.height - height),
                std::max(0.f, bounds.width - 2 * Style().scrollbarInset), height};
    }
    double ScrollBar::Fraction(float, float y) const
    {
        auto bounds = Bounds(), thumb = Thumb();
        return std::clamp((y - bounds.y - grab_ * thumb.height) / std::max(1.f, bounds.height - thumb.height), 0.f,
                          1.f);
    }
    bool ScrollBar::OnInput(const Input& input)
    {
        if (input.kind == InputKind::Down)
        {
            auto thumb = Thumb();
            grab_ = thumb.Contains(input.x, input.y) ? (input.y - thumb.y) / std::max(1.f, thumb.height) : .5f;
        }
        return Slider::OnInput(input);
    }
    void ScrollBar::OnRender(Canvas& canvas)
    {
        auto bounds = Bounds(), thumb = Thumb();
        canvas.Fill(bounds.x, bounds.y, bounds.width, bounds.height, Style().surface);
        canvas.Fill(thumb.x, thumb.y, thumb.width, thumb.height,
                    dragging_   ? Style().accent
                    : Hovered() ? Style().foreground
                                : Style().muted);
    }
    void ColorSwatch::OnRender(Canvas& c)
    {
        auto b = Bounds();
        // Standalone controls can receive styles without settings validation. Bound
        // work to logical pixels and use relative offsets so float addition progresses.
        const double cell = std::isfinite(Style().checkerSize) ? std::max(1.f, Style().checkerSize) : 1.f;
        bool darkRow = false;
        for (double y = 0; y < b.height; y += cell, darkRow = !darkRow)
        {
            bool dark = darkRow;
            for (double x = 0; x < b.width; x += cell, dark = !dark)
                c.Fill(float(b.x + x), float(b.y + y), float(cell), float(cell),
                       dark ? Style().checkerDark : Style().checkerLight);
        }
        c.Fill(b.x, b.y, b.width, b.height, value_);
    }
}  // namespace LWSUI
