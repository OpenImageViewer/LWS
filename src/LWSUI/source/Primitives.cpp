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
        DrawText(c, text_, b.x + p, b.y + p, b.width - 2 * p, b.height - 2 * p, Foreground(), wrap);
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
        return {available.width, std::max(Style().rowHeight, height + 2 * p)};
    }
    void Button::OnRender(Canvas& c)
    {
        auto b = Bounds();
        const bool enabled = Enabled();
        if (!flat || (enabled && (pressed_ || Hovered())))
            c.Fill(b.x, b.y, b.width, b.height,
                   !enabled    ? Style().surface
                   : pressed_  ? Style().selectedRow
                   : Hovered() ? Style().hoverSurface
                               : Style().line);
        const float inset = flat ? 0.f : Style().padding;
        c.CenteredText(text_, b.x + inset, b.y, b.width - 2 * inset, b.height, enabled ? Foreground() : Style().muted,
                       Font(), enabled ? HighlightSpans(text_) : std::vector<TextSpan>{});
        if (enabled && Host() && Host()->IsFocused(*this))
            c.Fill(b.x, b.y + b.height - Style().focusWidth, b.width, Style().focusWidth, Style().accent);
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
    TextBox::TextBox(std::string value) : text_(value), committed_(std::move(value))
    {
        SetContextMenuProvider([](Control& owner, const ContextMenuRequest& request)
                               { return static_cast<TextBox&>(owner).TextMenu(request); });
    }
    void TextBox::SelectAll()
    {
        ++pasteGeneration_;
        anchor_ = 0;
        caret_ = text_.size();
        UpdateGeometry();
        Invalidate();
    }
    void TextBox::Copy(bool cut)
    {
        if (!Host() || caret_ == anchor_ || (cut && readOnly_))
            return;
        const auto first = std::min(caret_, anchor_);
        const auto selected = std::string_view(text_).substr(first, std::max(caret_, anchor_) - first);
        if (LWS::SetClipboardText(Host()->Window(), selected) == LWS::ClipboardResult::Success && cut)
            Replace({});
    }
    void TextBox::Paste()
    {
        if (readOnly_ || !Host())
            return;
        const auto generation = ++pasteGeneration_;
        LWS::RequestClipboardText(Host()->Window(),
                                  [handle = Handle(), generation](auto result, std::string text)
                                  {
                                      auto* control = static_cast<TextBox*>(handle.Get());
                                      if (control && !control->readOnly_ && result == LWS::ClipboardResult::Success &&
                                          control->pasteGeneration_ == generation && control->Host() &&
                                          control->Host()->IsFocused(*control))
                                          control->Replace(std::move(text));
                                  });
    }
    std::vector<ContextMenuItem> TextBox::TextMenu(const ContextMenuRequest& request)
    {
        ++pasteGeneration_;
        if (!request.keyboard)
        {
            const auto at = Position(request.x);
            if (at < std::min(caret_, anchor_) || at >= std::max(caret_, anchor_))
                caret_ = anchor_ = at;
            UpdateGeometry();
            Invalidate();
        }
        const bool selected = caret_ != anchor_;
        return {{"Cut", "Ctrl+X", [](Control& c) { static_cast<TextBox&>(c).Copy(true); }, selected && !readOnly_},
                {"Copy", "Ctrl+C", [](Control& c) { static_cast<TextBox&>(c).Copy(false); }, selected},
                {"Paste", "Ctrl+V", [](Control& c) { static_cast<TextBox&>(c).Paste(); }, !readOnly_},
                ContextMenuItem::Separator(),
                {"Select All", "Ctrl+A", [](Control& c) { static_cast<TextBox&>(c).SelectAll(); }, !text_.empty()}};
    }
    void TextBox::SetReadOnly(bool value)
    {
        if (readOnly_ == value)
            return;
        readOnly_ = value;
        ++pasteGeneration_;
        if (value)
            Finish(EditPhase::Cancel);
        Invalidate();
    }
    void TextBox::SetText(std::string value)
    {
        ++pasteGeneration_;
        text_ = committed_ = std::move(value);
        caret_ = anchor_ = text_.size();
        editing_ = false;
        selecting_ = false;
        validation_.clear();
        geometryDirty_ = true;
        UpdateGeometry();
        Invalidate();
    }
    void TextBox::UpdateGeometry()
    {
        if (!Host())
            return;  // Detached controls can hold drafts without initializing a drawing backend.
        const bool rebuild = geometryDirty_ || geometryFont_ != Font();
        if (rebuild)
        {
            textWidth_ = Host()->MeasureText(text_, 100000, false, Font()).width;
            geometryFont_ = Font();
            geometryDirty_ = false;
        }
        if (rebuild || geometryCaret_ != caret_)
        {
            caretX_ = Host()->MeasureCaret(text_, caret_, Font());
            geometryCaret_ = caret_;
        }
        if (Bounds().width <= 0)
            return;
        const float caret = caretX_;
        const float visibleWidth = std::max(0.f, Bounds().width - 2 * Style().textPadding - Style().focusWidth);
        scroll_ = std::clamp(scroll_, 0.f, std::max(0.f, textWidth_ - visibleWidth));
        if (caret > scroll_ + visibleWidth)
            scroll_ = caret - visibleWidth;
        if (caret < scroll_)
            scroll_ = caret;
    }
    void TextBox::Replace(std::string text)
    {
        if (readOnly_)
            return;
        ++pasteGeneration_;
        if (!editing_)
        {
            committed_ = text_;
            editing_ = true;
        }
        auto first = std::min(caret_, anchor_), last = std::max(caret_, anchor_);
        text = SingleLine(std::move(text));
        text_.replace(first, last - first, text);
        caret_ = anchor_ = first + text.size();
        validation_.clear();
        geometryDirty_ = true;
        UpdateGeometry();
        OnEdit.Raise(text_, EditPhase::Preview);
        Invalidate();
    }
    bool TextBox::Finish(EditPhase phase)
    {
        ++pasteGeneration_;
        selecting_ = false;
        ReleaseCapture();
        if (!editing_)
            return true;
        if (phase == EditPhase::Commit && !validation_.empty())
            return false;
        if (phase == EditPhase::Cancel)
        {
            text_ = committed_;
            geometryDirty_ = true;
            validation_.clear();
        }
        editing_ = false;
        OnEdit.Raise(text_, phase);
        committed_ = text_;
        caret_ = anchor_ = text_.size();
        UpdateGeometry();
        Invalidate();
        return true;
    }
    size_t TextBox::Position(float x)
    {
        // Native input can arrive between two paints, including immediately after edits.
        UpdateGeometry();
        float at = x - Bounds().x - Style().textPadding + scroll_;
        return Host() ? Host()->HitTestText(text_, at, Font()) : text_.size();
    }
    void TextBox::OnRender(Canvas& c)
    {
        auto b = Bounds();
        const float p = Style().textPadding;
        const bool focused = Host() && Host()->IsFocused(*this);
        if (!borderless_)
            c.Fill(b.x, b.y, b.width, b.height, focused ? Style().line : Style().surface);
        auto spans = HighlightSpans(text_);
        if (focused && caret_ != anchor_)
            spans.push_back({{std::min(caret_, anchor_), std::max(caret_, anchor_) - std::min(caret_, anchor_)},
                             Style().selectionForeground,
                             Style().selection});
        if (text_.empty() && !focused)
            c.Text(placeholder_, b.x + p, b.y + p, b.width - 2 * p, b.height - 2 * p, Style().muted, Font());
        c.Text(text_, b.x + p - scroll_, b.y + p, b.width + scroll_ - 2 * p, b.height - 2 * p, Foreground(), Font(),
               false, spans);
        if (focused)
            c.Fill(b.x + p + caretX_ - scroll_, b.y + p, Style().borderWidth, b.height - 2 * p, Style().accent);
        if (!borderless_ && !validation_.empty())
            c.Fill(b.x, b.y + b.height - Style().focusWidth, b.width, Style().focusWidth, Style().error);
    }
    bool TextBox::OnInput(const Input& e)
    {
        UpdateGeometry();
        using K = LWS::KeyCode;
        if (readOnly_ && (e.kind == InputKind::Text ||
                          (e.kind == InputKind::KeyDown && (e.key == K::Backspace || e.key == K::Delete ||
                                                            (e.control && (e.key == K::X || e.key == K::V))))))
            return true;
        // A clipboard reply belongs to the exact editing session that requested it.
        if (e.kind == InputKind::Focus || e.kind == InputKind::Blur || e.kind == InputKind::Down ||
            (e.kind == InputKind::Move && selecting_))
            ++pasteGeneration_;
        if (e.kind == InputKind::Down)
        {
            caret_ = Position(e.x);
            if (!e.shift)
                anchor_ = caret_;
            selecting_ = true;
            Capture();
            UpdateGeometry();
            Invalidate();
            return true;
        }
        if (e.kind == InputKind::Move && selecting_)
        {
            caret_ = Position(e.x);
            UpdateGeometry();
            Invalidate();
            return true;
        }
        if (e.kind == InputKind::Up)
        {
            selecting_ = false;
            ReleaseCapture();
            return true;
        }
        if (e.kind == InputKind::Text)
        {
            Replace(e.text);
            return true;
        }
        if (e.kind == InputKind::Cancel)
            return Finish(EditPhase::Cancel);
        if (e.kind != InputKind::KeyDown)
            return false;
        if (e.key == K::Enter)
            return Finish(EditPhase::Commit);
        if (e.key == K::Escape)
            return Finish(EditPhase::Cancel);
        if (e.control && e.key == K::A)
            SelectAll();
        else if (e.control && (e.key == K::C || e.key == K::X))
            Copy(e.key == K::X);
        else if (e.control && e.key == K::V)
            Paste();
        else if (e.key == K::Backspace)
        {
            if (caret_ == anchor_)
                anchor_ = PreviousCharacter(text_, caret_);
            Replace({});
        }
        else if (e.key == K::Delete)
        {
            if (caret_ == anchor_)
                anchor_ = NextCharacter(text_, caret_);
            Replace({});
        }
        else if (e.key == K::Left || e.key == K::Right || e.key == K::Home || e.key == K::End)
        {
            ++pasteGeneration_;
            caret_ = e.key == K::Home   ? 0
                     : e.key == K::End  ? text_.size()
                     : e.key == K::Left ? PreviousCharacter(text_, caret_)
                                        : NextCharacter(text_, caret_);
            if (!e.shift)
                anchor_ = caret_;
        }
        else
            return false;
        UpdateGeometry();
        Invalidate();
        return true;
    }
    double Slider::Fraction(float x, float) const
    {
        return std::clamp((x - Bounds().x) / std::max(1.f, Bounds().width), 0.f, 1.f);
    }
    bool Slider::Finish(EditPhase phase)
    {
        if (dragging_)
        {
            dragging_ = false;
            ReleaseCapture();
            if (phase == EditPhase::Cancel)
                SetValue(start_);
            OnEdit.Raise(value_, phase);
        }
        return true;
    }
    bool Slider::OnInput(const Input& e)
    {
        if (e.kind == InputKind::Down)
        {
            start_ = value_;
            dragging_ = true;
            Capture();
        }
        if ((e.kind == InputKind::Down || e.kind == InputKind::Move) && dragging_)
        {
            SetValue(Fraction(e.x, e.y));
            OnEdit.Raise(value_, EditPhase::Preview);
            return true;
        }
        if (e.kind == InputKind::Up)
            return Finish(EditPhase::Commit);
        if (e.kind == InputKind::Cancel || (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::Escape))
            return Finish(EditPhase::Cancel);
        if (e.kind == InputKind::KeyDown && (e.key == LWS::KeyCode::Left || e.key == LWS::KeyCode::Right))
        {
            SetValue(value_ + (e.key == LWS::KeyCode::Left ? -1 : 1) * keyboardStep * (e.shift ? 10 : 1));
            OnEdit.Raise(value_, EditPhase::Commit);
            return true;
        }
        return false;
    }
    void Slider::OnRender(Canvas& c)
    {
        auto b = Bounds();
        if (filled)
        {
            c.Fill(b.x, b.y, b.width, b.height, Style().surface);
            c.Fill(b.x, b.y, float(value_) * b.width, b.height, Style().accent);
            return;
        }
        c.Fill(b.x, b.y + b.height / 2 - Style().sliderThickness / 2, b.width, Style().sliderThickness,
               Style().surface);
        c.Fill(b.x + float(value_) * std::max(0.f, b.width - Style().sliderThumbWidth), b.y + Style().labelPadding,
               Style().sliderThumbWidth, b.height - 2 * Style().labelPadding, Style().accent);
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
