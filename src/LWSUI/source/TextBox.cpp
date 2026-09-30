#include <LWSUI/Primitives.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWSUI/TextEditing.hpp>
#include <LWS/TextClipboard.hpp>
#include <LWS/Timer.hpp>
#include <algorithm>

namespace LWSUI
{
    namespace
    {
        std::string MultilineText(std::string_view text)
        {
            std::string result;
            result.reserve(text.size());
            for (size_t i = 0; i < text.size(); ++i)
            {
                const unsigned char c = text[i];
                if (c == '\r')
                {
                    result += '\n';
                    if (i + 1 < text.size() && text[i + 1] == '\n')
                        ++i;
                }
                else if (c == '\n' || c == '\t' || (c >= 32 && c != 127))
                    result += char(c);
            }
            return result;
        }
        class TextScrollBar final : public ScrollBar
        {
          public:

            bool Focusable() const override { return false; }
        };
    }  // namespace
    TextBox::TextBox(std::string value, TextBoxMode mode)
        : mode_(mode), text_(mode == TextBoxMode::Multiline ? MultilineText(value) : std::move(value)),
          committed_(text_)
    {
        if (mode_ == TextBoxMode::Multiline)
        {
            scrollbar_ = &Emplace<TextScrollBar>();
            scrollbar_->SetVisible(false);
            scrollConnection_ = scrollbar_->OnEdit.Connect(
                [this](double fraction, EditPhase)
                { SetScrollOffset(float(fraction) * std::max(0.f, contentHeight_ - viewport_.height)); });
        }
        SetContextMenuProvider([](Control& owner, const ContextMenuRequest& request)
                               { return static_cast<TextBox&>(owner).TextMenu(request); });
    }
    void TextBox::SelectAll()
    {
        ++pasteGeneration_;
        anchor_ = 0;
        caret_ = text_.size();
        caretUpstream_ = false;
        preferredX_.reset();
        UpdateGeometry(true);
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
            const auto position = Position(request.x, request.y);
            const auto at = position.offset;
            if (at < std::min(caret_, anchor_) || at >= std::max(caret_, anchor_))
            {
                caret_ = anchor_ = at;
                caretUpstream_ = position.upstream;
            }
            UpdateGeometry(true);
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
        StopSelecting();
        text_ = committed_ = mode_ == TextBoxMode::Multiline ? MultilineText(value) : std::move(value);
        caretUpstream_ = false;
        preferredX_.reset();
        caret_ = anchor_ = text_.size();
        editing_ = false;
        selecting_ = false;
        validation_.clear();
        geometryDirty_ = true;
        UpdateGeometry(true);
        Invalidate();
    }
    void TextBox::UpdateGeometry(bool reveal)
    {
        if (!Host())
            return;  // Detached controls can hold drafts without initializing a drawing backend.
        if (mode_ == TextBoxMode::Multiline)
        {
            const auto b = Bounds();
            const float p = Style().textPadding;
            const Size available{std::max(0.f, b.width - 2 * p), std::max(0.f, b.height - 2 * p)};
            const float gutter = Style().scrollbarWidth + Style().scrollbarMargin;
            const bool rebuild = geometryDirty_ || geometryFont_ != Font() || geometrySize_.width != available.width ||
                                 geometrySize_.height != available.height || geometryGutter_ != gutter;
            viewport_ = {b.x + p, b.y + p, available.width, available.height};
            if (rebuild)
            {
                lines_ = Host()->MeasureTextLines(text_, std::max(1.f, available.width), Font());
                contentHeight_ = lines_.empty() ? 0 : lines_.back().y + lines_.back().height;
                const bool overflow = contentHeight_ > available.height && available.width > gutter;
                scrollbar_->SetVisible(overflow);
                if (overflow)
                {
                    viewport_.width -= gutter;
                    lines_ = Host()->MeasureTextLines(text_, std::max(1.f, viewport_.width), Font());
                    contentHeight_ = lines_.empty() ? 0 : lines_.back().y + lines_.back().height;
                }
                scrollbar_->SetPage(available.height / std::max(1.f, contentHeight_));
                geometrySize_ = available;
                geometryGutter_ = gutter;
                geometryFont_ = Font();
                geometryDirty_ = false;
            }
            else if (scrollbar_->Visible())
                viewport_.width -= gutter;
            scrollbar_->Arrange(
                {b.x + b.width - p - Style().scrollbarWidth, viewport_.y, Style().scrollbarWidth, viewport_.height});
            caretBounds_ = Host()->MeasureTextCaret(text_, {caret_, caretUpstream_}, std::max(1.f, viewport_.width),
                                                    Font());
            float offset = scrollY_;
            if (reveal || rebuild)
            {
                if (caretBounds_.y < offset)
                    offset = caretBounds_.y;
                else if (caretBounds_.y + caretBounds_.height > offset + viewport_.height)
                    offset = caretBounds_.y + caretBounds_.height - viewport_.height;
            }
            SetScrollOffset(offset);
            return;
        }
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
        text = mode_ == TextBoxMode::Multiline ? MultilineText(text) : SingleLine(std::move(text));
        caretUpstream_ = false;
        preferredX_.reset();
        text_.replace(first, last - first, text);
        caret_ = anchor_ = first + text.size();
        validation_.clear();
        geometryDirty_ = true;
        UpdateGeometry(true);
        OnEdit.Raise(text_, EditPhase::Preview);
        Invalidate();
    }
    bool TextBox::Finish(EditPhase phase)
    {
        ++pasteGeneration_;
        StopSelecting();
        if (scrollbar_)
            scrollbar_->Finish(phase);
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
        caretUpstream_ = false;
        preferredX_.reset();
        caret_ = anchor_ = text_.size();
        UpdateGeometry(true);
        Invalidate();
        return true;
    }
    TextPosition TextBox::Position(float x, float y)
    {
        UpdateGeometry();
        if (!Host())
            return {text_.size(), false};
        if (mode_ == TextBoxMode::Multiline)
            return Host()->HitTestText(text_, x - viewport_.x, y - viewport_.y + scrollY_,
                                       std::max(1.f, viewport_.width), Font());
        return {Host()->HitTestText(text_, x - Bounds().x - Style().textPadding + scroll_, Font()), false};
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
        if (mode_ == TextBoxMode::Multiline)
        {
            {
                Canvas::ClipScope clip(c, viewport_.x, viewport_.y, viewport_.width, viewport_.height);
                if (text_.empty() && !focused)
                    c.Text(placeholder_, viewport_.x, viewport_.y, viewport_.width, viewport_.height, Style().muted,
                           Font(), true);
                c.Text(text_, viewport_.x, viewport_.y - scrollY_, viewport_.width,
                       std::max(contentHeight_, viewport_.height + scrollY_), Foreground(), Font(), true, spans);
                if (focused)
                    c.Fill(viewport_.x + std::min(caretBounds_.x, std::max(0.f, viewport_.width - Style().borderWidth)),
                           viewport_.y + caretBounds_.y - scrollY_, Style().borderWidth, caretBounds_.height,
                           Style().accent);
            }
            scrollbar_->Render(c);
        }
        else
        {
            if (text_.empty() && !focused)
                c.Text(placeholder_, b.x + p, b.y + p, b.width - 2 * p, b.height - 2 * p, Style().muted, Font());
            c.Text(text_, b.x + p - scroll_, b.y + p, b.width + scroll_ - 2 * p, b.height - 2 * p, Foreground(), Font(),
                   false, spans);
            if (focused)
                c.Fill(b.x + p + caretX_ - scroll_, b.y + p, Style().borderWidth, b.height - 2 * p, Style().accent);
        }
        if (!borderless_ && !validation_.empty())
            c.Fill(b.x, b.y + b.height - Style().focusWidth, b.width, Style().focusWidth, Style().error);
    }
    bool TextBox::OnInput(const Input& e)
    {
        UpdateGeometry();
        using K = LWS::KeyCode;
        if (readOnly_ &&
            (e.kind == InputKind::Text ||
             (e.kind == InputKind::KeyDown && (e.key == K::Backspace || e.key == K::Delete ||
                                               (mode_ == TextBoxMode::Multiline && e.key == K::Enter && !e.control) ||
                                               (e.control && (e.key == K::X || e.key == K::V))))))
            return true;
        if (e.kind == InputKind::Focus || e.kind == InputKind::Blur || e.kind == InputKind::Down ||
            (e.kind == InputKind::Move && selecting_))
            ++pasteGeneration_;
        if (e.kind == InputKind::Down)
        {
            preferredX_.reset();
            SetCaret(Position(e.x, e.y), e.shift);
            selecting_ = true;
            Capture();
            return true;
        }
        if (e.kind == InputKind::Move && selecting_)
        {
            SelectPointer(e.x, e.y);
            return true;
        }
        if (e.kind == InputKind::Up)
        {
            StopSelecting();
            return true;
        }
        if (e.kind == InputKind::Wheel && mode_ == TextBoxMode::Multiline)
        {
            SetScrollOffset(scrollY_ - e.wheel * Style().scrollStep);
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
        {
            if (mode_ == TextBoxMode::Multiline && !e.control)
            {
                Replace("\n");
                return true;
            }
            return Finish(EditPhase::Commit);
        }
        if (e.key == K::Escape)
            return Finish(EditPhase::Cancel);
        if (e.control && e.key == K::A)
            SelectAll();
        else if (e.control && (e.key == K::C || e.key == K::X))
            Copy(e.key == K::X);
        else if (e.control && e.key == K::V)
            Paste();
        else if (e.key == K::Backspace || e.key == K::Delete)
        {
            if (caret_ == anchor_)
                anchor_ = e.key == K::Backspace ? PreviousCharacter(text_, caret_) : NextCharacter(text_, caret_);
            Replace({});
        }
        else if (mode_ == TextBoxMode::Multiline && !lines_.empty() &&
                 (e.key == K::Up || e.key == K::Down || e.key == K::PageUp || e.key == K::PageDown))
        {
            if (!preferredX_)
                preferredX_ = caretBounds_.x;
            const bool up = e.key == K::Up || e.key == K::PageUp;
            size_t target = CaretLine();
            if (e.key == K::Up || e.key == K::Down)
                target = up ? (target ? target - 1 : 0) : std::min(target + 1, lines_.size() - 1);
            else
            {
                const float y = caretBounds_.y + (up ? -1 : 1) * std::max(caretBounds_.height, viewport_.height);
                while (up && target && lines_[target].y > y)
                    --target;
                while (!up && target + 1 < lines_.size() && lines_[target].y < y)
                    ++target;
            }
            const auto& line = lines_[target];
            SetCaret(Host()->HitTestText(text_, *preferredX_, line.y + line.height / 2, std::max(1.f, viewport_.width),
                                         Font()),
                     e.shift);
        }
        else if (e.key == K::Left || e.key == K::Right || e.key == K::Home || e.key == K::End)
        {
            preferredX_.reset();
            TextPosition position;
            if (mode_ == TextBoxMode::Multiline && !e.control && !lines_.empty() &&
                (e.key == K::Home || e.key == K::End))
            {
                const auto& line = lines_[CaretLine()];
                position = {line.range.offset + (e.key == K::End ? line.range.length : 0), e.key == K::End};
            }
            else
                position = {e.key == K::Home   ? 0
                            : e.key == K::End  ? text_.size()
                            : e.key == K::Left ? PreviousCharacter(text_, caret_)
                                               : NextCharacter(text_, caret_),
                            e.key == K::Left};
            SetCaret(position, e.shift);
        }
        else
            return false;
        return true;
    }
    TextBox::~TextBox()
    {
        StopSelecting();
    }
    void TextBox::OnDetach()
    {
        StopSelecting();
        // A detached control may outlive its original platform or be attached to another one.
        selectionTimer_.reset();
    }
    void TextBox::SetVisibleLines(unsigned lines)
    {
        visibleLines_ = std::max(1U, lines);
        Invalidate(true);
    }
    Size TextBox::OnMeasure(Size available)
    {
        if (mode_ == TextBoxMode::SingleLine)
            return Control::OnMeasure(available);
        const float line = Host() ? Host()->MeasureText("Mg", available.width, false, Font()).height
                                  : Font().size * 1.4f;
        return {available.width, std::min(available.height, visibleLines_ * line + 2 * Style().textPadding)};
    }
    void TextBox::SetCaret(TextPosition position, bool extend)
    {
        ++pasteGeneration_;
        caret_ = std::min(position.offset, text_.size());
        caretUpstream_ = position.upstream;
        if (!extend)
            anchor_ = caret_;
        UpdateGeometry(true);
        Invalidate();
    }
    size_t TextBox::CaretLine() const
    {
        size_t line = 0;
        while (line + 1 < lines_.size() && lines_[line + 1].y <= caretBounds_.y + .01f)
            ++line;
        return line;
    }
    void TextBox::SetScrollOffset(float offset)
    {
        const float next = std::clamp(offset, 0.f, std::max(0.f, contentHeight_ - viewport_.height));
        if (next != scrollY_)
        {
            scrollY_ = next;
            Invalidate();
        }
        const double fraction = scrollY_ / std::max(1.f, contentHeight_ - viewport_.height);
        if (scrollbar_ && scrollbar_->Value() != fraction)
            scrollbar_->SetValue(fraction);
    }
    void TextBox::StopSelecting()
    {
        selecting_ = false;
        if (selectionTimer_)
            selectionTimer_->Enable(false);
        ReleaseCapture();
    }
    void TextBox::SelectPointer(float x, float y)
    {
        pointerX_ = x;
        pointerY_ = y;
        if (mode_ == TextBoxMode::Multiline && Host())
        {
            const float outside = y < viewport_.y                      ? y - viewport_.y
                                  : y > viewport_.y + viewport_.height ? y - viewport_.y - viewport_.height
                                                                       : 0;
            SetScrollOffset(scrollY_ + std::clamp(outside, -Style().scrollStep, Style().scrollStep));
            if (outside && !selectionTimer_)
            {
                selectionTimer_ = std::make_unique<LWS::HighPrecisionTimer>(Host()->Window().GetPlatformContext(),
                                                                            [this]
                                                                            {
                                                                                if (selecting_)
                                                                                    SelectPointer(pointerX_, pointerY_);
                                                                            });
                selectionTimer_->SetDueTime(16);
                selectionTimer_->SetRepeatInterval(16);
            }
            if (selectionTimer_)
                selectionTimer_->Enable(outside != 0);
            y = std::clamp(y, viewport_.y, viewport_.y + std::max(0.f, viewport_.height - 1));
        }
        SetCaret(Position(x, y), true);
    }
}  // namespace LWSUI
