#include <LWSUI/Primitives.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWSUI/TextEditing.hpp>
#include <LWSUI/internal/TextPresentation.hpp>
#include <LWS/TextClipboard.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <sstream>
namespace LWSUI
{
    void Label::OnRender(Canvas& c)
    {
        auto b = Bounds();
        const float p = Style().labelPadding;
        const float width = std::max(0.f, b.width - 2 * p);
        const std::string text = trimming == TextTrimming::Ellipsis && !wrap
                                     ? internal::EllipsizeText(c, text_, width, Font())
                                     : text_;
        const auto textBounds = internal::AlignedTextBounds(
            c, text, {b.x + p, b.y + p, width, std::max(0.f, b.height - 2 * p)}, Font(),
            Alignment::Start, verticalAlignment, wrap);
        DrawText(c, text, textBounds.x, textBounds.y, textBounds.width, textBounds.height, Foreground(), wrap);
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
            const auto scale = host->DrawingScale();
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
        const auto textBounds = internal::AlignedTextBounds(
            canvas, text, {b.x + inset, b.y, b.width - inset, b.height}, Font(),
            Alignment::Start, Alignment::Center, /*wrap=*/false, /*constrainHeight=*/false);
        DrawText(canvas, text, textBounds.x, textBounds.y, textBounds.width, textBounds.height, Foreground());
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
        const auto textBounds = internal::AlignedTextBounds(
            canvas, text_, {b.x + inset, b.y, b.width - inset, b.height}, Font(),
            Alignment::Start, Alignment::Center, /*wrap=*/false, /*constrainHeight=*/false);
        DrawText(canvas, text_, textBounds.x, textBounds.y, textBounds.width, textBounds.height,
                 Enabled() ? Foreground() : Style().muted);
    }
    void ScrollBar::SetPage(double page)
    {
        if (!std::isfinite(page)) throw std::invalid_argument("Scrollbar page must be finite");
        page = std::clamp(page, 0., 1.);
        if (page != page_ && resolutionDragging_) Finish(EditPhase::Cancel);
        page_ = page;
        Invalidate();
    }
    void ScrollBar::OnArrange()
    {
        const auto b = Bounds();
        if (resolutionDragging_ && (b.x != arrangedBounds_.x || b.y != arrangedBounds_.y ||
            b.width != arrangedBounds_.width || b.height != arrangedBounds_.height))
            Finish(EditPhase::Cancel);
        arrangedBounds_ = b;
        Slider::OnArrange();
    }
    void ScrollBar::SetInwardDistance(double fraction)
    {
        if (!std::isfinite(fraction) || fraction <= 0 || fraction > 1)
            throw std::invalid_argument("Inward distance must be greater than zero and at most one");
        if (fraction == inwardDistance_) return;
        Finish(EditPhase::Cancel);
        inwardDistance_ = fraction;
    }
    void ScrollBar::SetDynamicResolution(bool enabled)
    {
        if (dynamicResolution_ == enabled) return;
        Finish(EditPhase::Cancel);
        dynamicResolution_ = enabled;
    }
    void ScrollBar::ConfigureResolution(Rect bounds, double range, double line, ScrollResolution mode)
    {
        if (!std::isfinite(range) || range < 0 || !std::isfinite(line) || line <= 0 ||
            !std::isfinite(bounds.x) || !std::isfinite(bounds.y) || !std::isfinite(bounds.width) ||
            !std::isfinite(bounds.height) || bounds.width < 0 || bounds.height < 0)
            throw std::invalid_argument("Invalid scrollbar resolution");
        if (bounds.x != resolutionBounds_.x || bounds.y != resolutionBounds_.y ||
            bounds.width != resolutionBounds_.width || bounds.height != resolutionBounds_.height ||
            range != contentRange_ || line != lineSize_ || mode != resolutionMode_)
            Finish(EditPhase::Cancel);
        resolutionBounds_ = bounds;
        contentRange_ = range;
        lineSize_ = line;
        resolutionMode_ = mode;
    }
    void ScrollBar::UpdateSpeed(float x, float y)
    {
        const auto b = Bounds(), thumb = Thumb(), area = resolutionBounds_;
        const bool vertical = orientation_ == Orientation::Vertical;
        const float center = vertical ? area.x + area.width / 2 : area.y + area.height / 2;
        const float barCenter = vertical ? b.x + b.width / 2 : b.y + b.height / 2;
        const float edge = vertical ? (barCenter >= center ? b.x : b.x + b.width)
                                    : (barCenter >= center ? b.y : b.y + b.height);
        const float distance = float(inwardDistance_ * (vertical ? area.width : area.height)) * (barCenter >= center ? -1.f : 1.f);
        inward_ = std::abs(distance) > 0 ? std::clamp(((vertical ? x : y) - edge) / distance, 0.f, 1.f) : 0;
        const double travel = std::max(1.f, vertical ? b.height - thumb.height : b.width - thumb.width);
        const double normal = contentRange_ / travel;
        // Horizontal row mode estimates a line using row height.
        // TODO: Find a better horizontal unit based on column widths or another meaningful measure.
        const double fine = std::min(normal, resolutionMode_ == ScrollResolution::Row ? lineSize_ : 1.);
        sensitivity_ = normal > 0 ? normal * std::pow(fine / normal, inward_) : 0;
        const double end = resolutionMode_ == ScrollResolution::Row ? 1. : 1. / lineSize_;
        speed_ = fine == normal ? 100 : 100 * std::pow(std::min(100., end) / 100, inward_);
        OnScrollSpeed.Raise(speed_);
        Invalidate();
    }
    bool ScrollBar::Finish(EditPhase phase)
    {
        const bool notify = resolutionDragging_ || speed_ != 100;
        resolutionDragging_ = false;
        const bool result = Slider::Finish(phase);
        inward_ = 0;
        speed_ = 100;
        if (notify) OnScrollSpeed.Raise(speed_);
        return result;
    }
    Rect ScrollBar::Thumb() const
    {
        const auto b = Bounds();
        const bool vertical = orientation_ == Orientation::Vertical;
        const float extent = vertical ? b.height : b.width;
        const float length = std::min(extent, std::max(std::min(Style().scrollbarMinThumb, extent / 2), extent * float(page_)));
        const float position = float(value_) * (extent - length);
        const float inset = Style().scrollbarInset;
        return vertical ? Rect{b.x + inset, b.y + position, std::max(0.f, b.width - 2 * inset), length}
                        : Rect{b.x + position, b.y + inset, length, std::max(0.f, b.height - 2 * inset)};
    }
    double ScrollBar::Fraction(float x, float y) const
    {
        const auto b = Bounds(), thumb = Thumb();
        const bool vertical = orientation_ == Orientation::Vertical;
        const float point = vertical ? y : x, origin = vertical ? b.y : b.x;
        const float length = vertical ? thumb.height : thumb.width, extent = vertical ? b.height : b.width;
        return std::clamp((point - origin - grab_ * length) / std::max(1.f, extent - length), 0.f, 1.f);
    }
    bool ScrollBar::OnInput(const Input& input)
    {
        if ((input.kind == InputKind::Down || input.kind == InputKind::Up) && input.button != LWS::MouseButton::Left)
            return false;
        if (input.kind == InputKind::Down && dragging_) return true;
        if ((input.kind == InputKind::Move || input.kind == InputKind::Up) && resolutionDragging_ && dragging_)
        {
            UpdateSpeed(input.x, input.y);
            if (!resolutionDragging_ || !dragging_) return true;
            const float axis = orientation_ == Orientation::Vertical ? input.y : input.x;
            const double delta = axis - lastAxis_;
            lastAxis_ = axis;
            if (delta != 0 && contentRange_ > 0)
            {
                value_ = std::clamp(value_ + delta * sensitivity_ / contentRange_, 0., 1.);
                OnEdit.Raise(value_, EditPhase::Preview);
            }
            if (input.kind == InputKind::Up && dragging_) Finish(EditPhase::Commit);
            return true;
        }
        if (input.kind == InputKind::Down)
        {
            const auto thumb = Thumb();
            const bool onThumb = thumb.Contains(input.x, input.y);
            const bool vertical = orientation_ == Orientation::Vertical;
            grab_ = onThumb
                ? ((vertical ? input.y - thumb.y : input.x - thumb.x) / std::max(1.f, vertical ? thumb.height : thumb.width)) : .5f;
            resolutionDragging_ = dynamicResolution_ && contentRange_ > 0;
            lastAxis_ = orientation_ == Orientation::Vertical ? input.y : input.x;
            if (resolutionDragging_)
            {
                start_ = value_;
                dragging_ = true;
                Capture();
                UpdateSpeed(input.x, input.y);
                if (resolutionDragging_ && dragging_ && !onThumb)
                {
                    value_ = Fraction(input.x, input.y);
                    OnEdit.Raise(value_, EditPhase::Preview);
                    Invalidate();
                }
                return true;
            }
        }
        if (orientation_ == Orientation::Vertical && input.kind == InputKind::KeyDown)
        {
            auto translated = input;
            if (input.key == LWS::KeyCode::Up) translated.key = LWS::KeyCode::Left;
            if (input.key == LWS::KeyCode::Down) translated.key = LWS::KeyCode::Right;
            return Slider::OnInput(translated);
        }
        return Slider::OnInput(input);
    }
    void ScrollBar::RenderResolutionGuide(Canvas& canvas) const
    {
        if (!resolutionDragging_) return;
        const auto area = resolutionBounds_, bar = Bounds();
        const auto& theme = Style();
        const bool vertical = orientation_ == Orientation::Vertical;
        const float cx = area.x + area.width / 2, cy = area.y + area.height / 2;
        const float barCenter = vertical ? bar.x + bar.width / 2 : bar.y + bar.height / 2;
        const float center = vertical ? cx : cy;
        const float edge = vertical ? (barCenter >= center ? bar.x : bar.x + bar.width)
                                    : (barCenter >= center ? bar.y : bar.y + bar.height);
        const float direction = barCenter >= center ? -1.f : 1.f;
        const float target = edge + direction * float(inwardDistance_ * (vertical ? area.width : area.height));
        const float marker = edge + (target - edge) * inward_;
        Canvas::ClipScope clip(canvas, area.x, area.y, area.width, area.height);
        if (vertical)
        {
            canvas.Line(edge, cy, target, cy, 2, theme.accent);
            for (float y = area.y; y < area.y + area.height; y += 10)
                canvas.Line(target, y, target, std::min(y + 5, area.y + area.height), 1, theme.accent);
            canvas.FillCircle(marker, cy, 5, theme.accent);
        }
        else
        {
            canvas.Line(cx, edge, cx, target, 2, theme.accent);
            for (float x = area.x; x < area.x + area.width; x += 10)
                canvas.Line(x, target, std::min(x + 5, area.x + area.width), target, 1, theme.accent);
            canvas.FillCircle(cx, marker, 5, theme.accent);
        }
        std::ostringstream text;
        text.precision(3);
        text << "Speed " << speed_ << "%";
        const float width = std::min(120.f, area.width), height = std::min(24.f, area.height);
        const float x = vertical ? marker - width : cx - width - 5;
        const float y = vertical ? cy - height - 4 : marker - height;
        Rect box{std::clamp(x, area.x, area.x + area.width - width),
                 std::clamp(y, area.y, area.y + area.height - height), width, height};
        canvas.Fill(box.x, box.y, box.width, box.height, theme.popupSurface);
        canvas.Text(text.str(), box.x + 4, box.y, std::max(0.f, box.width - 8), box.height, theme.foreground, theme.smallFont);
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
