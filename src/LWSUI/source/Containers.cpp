#include <LWSUI/internal/TextPresentation.hpp>
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <algorithm>
#include <stdexcept>
namespace LWSUI
{
    Size StackPanel::OnMeasure(Size available)
    {
        Size result{};
        const size_t count = std::count_if(Children().begin(), Children().end(),
                                           [](const auto& child) { return child->Visible(); });
        auto childSpace = available;
        if (orientation == Orientation::Horizontal && sizeToContent)
            childSpace.width = fallbackItemWidth;
        else if (orientation == Orientation::Horizontal && count)
            childSpace.width = std::max(0.f, (available.width - spacing * float(count - 1)) / float(count));
        for (auto& child : Children())
            if (child->Visible())
            {
                auto size = sizeToContent ? child->MeasureContent(childSpace) : child->Measure(childSpace);
                if (orientation == Orientation::Vertical)
                {
                    result.height += size.height;
                    result.width = std::max(result.width, size.width);
                }
                else
                {
                    result.width += size.width;
                    result.height = std::max(result.height, size.height);
                }
            }
        if (count > 1)
        {
            if (orientation == Orientation::Vertical)
                result.height += (count - 1) * spacing;
            else
                result.width += (count - 1) * spacing;
        }
        return result;
    }
    void StackPanel::OnArrange()
    {
        auto b = Bounds();
        float position = orientation == Orientation::Vertical ? b.y : b.x;
        size_t visible = 0;
        for (auto& child : Children())
            visible += child->Visible();
        for (auto& child : Children())
            if (child->Visible())
            {
                auto size = child->DesiredSize();
                if (orientation == Orientation::Vertical)
                {
                    child->Arrange({b.x, position, b.width, size.height});
                    position += size.height + spacing;
                }
                else
                {
                    float width = std::max(0.f, (b.width - spacing * float(visible ? visible - 1 : 0)) /
                                                    std::max(size_t{1}, visible));
                    if (sizeToContent)
                    {
                        const float gaps = spacing * float(visible ? visible - 1 : 0);
                        const float scale = clipPartialChildren
                                                ? 1.f
                                                : std::clamp((b.width - gaps) /
                                                                 std::max(1.f, DesiredSize().width - gaps),
                                                             0.f, 1.f);
                        width = size.width * scale;
                    }
                    if (clipPartialChildren && position + width > b.x + b.width)
                        width = 0;
                    child->Arrange({position, b.y, width, b.height});
                    position += width + spacing;
                }
            }
    }
    static std::vector<float> ColumnWidths(const std::vector<float>& columns, float width, float spacing)
    {
        float fixed = 0, weight = 0;
        for (float c : columns)
        {
            if (c >= 0)
                fixed += c;
            else
                weight -= c;
        }
        float remaining = std::max(0.f, width - fixed - spacing * float(columns.size() - 1));
        std::vector<float> result;
        result.reserve(columns.size());
        for (float c : columns)
            result.push_back(c >= 0 ? c : remaining * (-c) / weight);
        return result;
    }
    Size Grid::OnMeasure(Size available)
    {
        rowHeights_.clear();
        if (columns.empty())
            return {};
        auto widths = ColumnWidths(columns, available.width, spacing);
        rowHeights_.resize((Children().size() + columns.size() - 1) / columns.size());
        for (size_t i = 0; i < Children().size(); ++i)
        {
            auto size = Children()[i]->Measure({widths[i % widths.size()], available.height});
            rowHeights_[i / columns.size()] = std::max(rowHeights_[i / columns.size()], size.height);
        }
        float height = 0;
        for (float row : rowHeights_)
            height += row;
        if (!rowHeights_.empty())
            height += spacing * (rowHeights_.size() - 1);
        return {available.width, height};
    }
    void Grid::OnArrange()
    {
        if (columns.empty() || rowHeights_.empty())
            return;
        auto bounds = Bounds();
        auto widths = ColumnWidths(columns, bounds.width, spacing);
        float x = bounds.x, y = bounds.y;
        for (size_t i = 0; i < Children().size(); ++i)
        {
            size_t column = i % columns.size(), row = i / columns.size();
            if (column == 0 && i != 0)
            {
                x = bounds.x;
                y += rowHeights_[row - 1] + spacing;
            }
            float height = std::min(rowHeights_[row], Children()[i]->DesiredSize().height);
            Children()[i]->Arrange({x, y, widths[column], height});
            x += widths[column] + spacing;
        }
    }
    ScrollView::ScrollView()
    {
        bar_ = &Emplace<ScrollBar>();
        horizontal_ = &Emplace<ScrollBar>();
        horizontal_->SetOrientation(Orientation::Horizontal);
        horizontal_->SetVisible(false);
        connection_ = bar_->OnEdit.Connect([this](double value, EditPhase)
            { SetScrollOffset(offset_.width, float(value * std::max(0.f, extent_.height - viewport_.height))); });
        horizontalConnection_ = horizontal_->OnEdit.Connect([this](double value, EditPhase)
            { SetScrollOffset(float(value * std::max(0.f, extent_.width - viewport_.width)), offset_.height); });
        verticalSpeed_ = bar_->OnScrollSpeed.Connect([this](double speed) { Invalidate(); OnScrollSpeed.Raise(speed); });
        horizontalSpeed_ = horizontal_->OnScrollSpeed.Connect([this](double speed) { Invalidate(); OnScrollSpeed.Raise(speed); });
    }
    void ScrollView::SetOptions(ScrollViewOptions options)
    {
        if (!std::isfinite(options.inwardDistance) || options.inwardDistance <= 0 || options.inwardDistance > 1)
            throw std::invalid_argument("Invalid inward distance");
        bar_->Finish(EditPhase::Cancel);
        horizontal_->Finish(EditPhase::Cancel);
        options_ = options;
        if (!options.horizontalScroll) offset_.width = 0;
        Invalidate(true);
    }
    void ScrollView::SetContentMinimumSize(Size size)
    {
        if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width < 0 || size.height < 0)
            throw std::invalid_argument("Invalid minimum content size");
        bar_->Finish(EditPhase::Cancel);
        horizontal_->Finish(EditPhase::Cancel);
        minimumContent_ = size;
        Invalidate(true);
    }
    void ScrollView::SetContent(std::unique_ptr<Control> content)
    {
        bar_->Finish(EditPhase::Cancel);
        horizontal_->Finish(EditPhase::Cancel);
        if (content_) Remove(*content_);
        content_ = content ? &Add(std::move(content)) : nullptr;
        offset_ = {};
        extent_ = {};
        Invalidate(true);
    }
    void ScrollView::LayoutContent(Size available)
    {
        const float gutter = Style().scrollbarWidth + Style().scrollbarMargin;
        const float width = std::max(0.f, available.width - (overlayScrollBar ? 0 : gutter));
        const float contentWidth = options_.horizontalScroll ? std::max(width, minimumContent_.width) : width;
        const auto measured = content_ ? content_->Measure({contentWidth, std::max(available.height, minimumContent_.height)}) : Size{};
        extent_ = content_ ? Size{options_.horizontalScroll ? std::max(contentWidth, measured.width) : width,
                                 std::max(minimumContent_.height, measured.height)} : Size{};
        const bool horizontal = options_.horizontalScroll && extent_.width > width;
        viewport_ = {Bounds().x, Bounds().y, width,
                     std::max(0.f, available.height - (horizontal && !overlayScrollBar ? gutter : 0))};
        measuredAvailable_ = available;
        measuredRevision_ = LayoutRevision();
        measuredGutter_ = gutter;
        measuredOverlay_ = overlayScrollBar;
    }
    Size ScrollView::OnMeasure(Size available)
    {
        LayoutContent(available);
        bar_->Measure({Style().scrollbarWidth, available.height});
        horizontal_->Measure({available.width, Style().scrollbarWidth});
        return {available.width, std::min(available.height, extent_.height)};
    }
    void ScrollView::OnArrange()
    {
        const auto b = Bounds();
        // Offset-only moves reuse measured extents. Descendant layout invalidation,
        // changed viewport dimensions and scrollbar policy invalidate this cache.
        if (!measuredAvailable_ || measuredAvailable_->width != b.width || measuredAvailable_->height != b.height ||
            measuredRevision_ != LayoutRevision() || measuredOverlay_ != overlayScrollBar ||
            measuredGutter_ != Style().scrollbarWidth + Style().scrollbarMargin)
            LayoutContent({b.width, b.height});
        viewport_.x = b.x;
        viewport_.y = b.y;
        offset_.width = std::clamp(offset_.width, 0.f, std::max(0.f, extent_.width - viewport_.width));
        offset_.height = std::clamp(offset_.height, 0.f, std::max(0.f, extent_.height - viewport_.height));
        if (content_) content_->Arrange({b.x - offset_.width, b.y - offset_.height, extent_.width, extent_.height});
        bar_->SetDynamicResolution(options_.dynamicResolution);
        horizontal_->SetDynamicResolution(options_.dynamicResolution);
        bar_->SetInwardDistance(options_.inwardDistance);
        horizontal_->SetInwardDistance(options_.inwardDistance);
        bar_->ConfigureResolution(b, std::max(0.f, extent_.height - viewport_.height), Style().scrollStep, options_.resolution);
        horizontal_->ConfigureResolution(b, std::max(0.f, extent_.width - viewport_.width), Style().scrollStep, options_.resolution);
        bar_->SetVisible(extent_.height > viewport_.height);
        horizontal_->SetVisible(options_.horizontalScroll && extent_.width > viewport_.width);
        bar_->SetPage(viewport_.height / std::max(1.f, extent_.height));
        horizontal_->SetPage(viewport_.width / std::max(1.f, extent_.width));
        bar_->SetValue(offset_.height / std::max(1.f, extent_.height - viewport_.height));
        horizontal_->SetValue(offset_.width / std::max(1.f, extent_.width - viewport_.width));
        const float bottom = horizontal_->Visible() ? Style().scrollbarWidth + Style().scrollbarMargin : 0;
        bar_->Arrange({b.x + b.width - Style().scrollbarWidth - Style().scrollbarMargin, b.y,
                       Style().scrollbarWidth, std::max(0.f, b.height - bottom)});
        horizontal_->Arrange({b.x, b.y + b.height - Style().scrollbarWidth - Style().scrollbarMargin,
                              std::max(0.f, b.width - Style().scrollbarWidth - Style().scrollbarMargin), Style().scrollbarWidth});
    }
    Control* ScrollView::HitTest(float x, float y)
    {
        if (!Control::HitTest(x, y)) return nullptr;
        if (auto* hit = bar_->HitTest(x, y)) return hit;
        if (auto* hit = horizontal_->HitTest(x, y)) return hit;
        if (content_ && viewport_.Contains(x, y))
            if (auto* hit = content_->HitTest(x, y)) return hit;
        return hitTestBackground ? this : nullptr;
    }
    void ScrollView::OnRender(Canvas& canvas)
    {
        {
            Canvas::ClipScope clip(canvas, viewport_.x, viewport_.y, viewport_.width, viewport_.height);
            if (content_) content_->Render(canvas);
            if (options_.resolutionGuide)
            {
                bar_->RenderResolutionGuide(canvas);
                horizontal_->RenderResolutionGuide(canvas);
            }
        }
        bar_->Render(canvas);
        horizontal_->Render(canvas);
    }
    void ScrollView::SetOffset(float offset) { SetScrollOffset(offset_.width, offset); }
    void ScrollView::SetScrollOffset(float x, float y)
    {
        if (!std::isfinite(x) || !std::isfinite(y)) throw std::invalid_argument("Scroll offsets must be finite");
        const Size next{options_.horizontalScroll ? std::clamp(x, 0.f, std::max(0.f, extent_.width - viewport_.width)) : 0,
                        std::clamp(y, 0.f, std::max(0.f, extent_.height - viewport_.height))};
        if (next.width == offset_.width && next.height == offset_.height) return;
        offset_ = next;
        OnArrange();
        Invalidate();
        OnScroll.Raise(offset_);
    }
    bool ScrollView::OnInput(const Input& input)
    {
        if (input.kind != InputKind::Wheel) return false;
        SetScrollOffset(offset_.width - (input.shift && options_.horizontalScroll ? input.wheel * Style().scrollStep : 0),
                        offset_.height - (!input.shift || !options_.horizontalScroll ? input.wheel * Style().scrollStep : 0));
        return true;
    }

    namespace
    {
        int TreeDepth(const Control& control)
        {
            int depth = 0;
            for (auto* parent = control.Parent(); parent; parent = parent->Parent())
                if (dynamic_cast<const TreeView::Branch*>(parent))
                    ++depth;
            return depth;
        }
        void PaintTreeRow(Control& control, Canvas& canvas)
        {
            auto bounds = control.Bounds();
            auto& style = control.Style();
            if (control.Host() && control.Host()->HasFocusWithin(control))
                canvas.Fill(bounds.x + style.treeRowInset, bounds.y, bounds.width - 2 * style.treeRowInset,
                            bounds.height, style.selectedRow);
            else if (control.Host() && control.Host()->HasHoverWithin(control))
                canvas.Fill(bounds.x + style.treeRowInset, bounds.y, bounds.width - 2 * style.treeRowInset,
                            bounds.height, style.hoveredRow);
            canvas.Fill(bounds.x + style.treeLineInset, bounds.y + bounds.height - style.borderWidth,
                        bounds.width - 2 * style.treeLineInset, style.borderWidth, style.line);
        }
        class TreeHeading : public Button
        {
          public:

            explicit TreeHeading(std::string text) : Button(std::move(text)) { fontRole = FontRole::Heading; }

          protected:

            Size OnMeasure(Size size) override
            {
                const float height = Host() ? Host()->MeasureText("Mg", size.width, false, Font()).height
                                            : Font().size * 1.4f;
                return {size.width, std::max(Style().treeRowHeight, height + 2 * Style().treeRowPadding)};
            }
            void OnRender(Canvas& canvas) override
            {
                PaintTreeRow(*this, canvas);
                auto b = Bounds();
                const float editorLeft = std::max(Style().editorMinLeft, b.width * Style().editorFraction);
                const float indent = std::min(std::max(0, TreeDepth(*this) - 1) * Style().treeIndent,
                                              std::max(0.f, editorLeft - Style().editorMinLeft));
                const auto glyph = internal::AlignedTextBounds(
                    canvas, text_.substr(0, 1), {b.x + Style().treeLabelInset + indent, b.y, Style().checkboxSize, b.height},
                    Font(), Alignment::Start, Alignment::Center);
                canvas.Text(text_.substr(0, 1), glyph.x, glyph.y, glyph.width, glyph.height, Style().accent, Font());
                const float left = Style().treeLabelInset + Style().checkboxSize + indent;
                const auto label = internal::AlignedTextBounds(
                    canvas, text_.substr(3), {b.x + left, b.y, b.width - left - Style().padding, b.height},
                    Font(), Alignment::Start, Alignment::Center);
                DrawText(canvas, text_.substr(3), label.x, label.y, label.width, label.height, Style().accent);
            }
        };
        Control* FirstFocusable(Control& control)
        {
            if (!control.Visible() || !control.Enabled())
                return nullptr;
            if (control.Focusable())
                return &control;
            if (auto* parent = dynamic_cast<Container*>(&control))
                for (auto& child : parent->Children())
                    if (auto* found = FirstFocusable(*child))
                        return found;
            return nullptr;
        }
    }  // namespace
    TreeView::Row::Row(std::unique_ptr<Control> name, std::unique_ptr<Control> editor, std::unique_ptr<Control> action)
    {
        if (!name || !editor)
            throw std::invalid_argument("A property row needs a name and an editor");
        Add(std::move(name));
        Add(std::move(editor));
        if (action)
            Add(std::move(action));
    }
    TreeView::Row::Columns TreeView::Row::ColumnLayout(float available) const
    {
        const auto& s = Style();
        const float width = std::max(0.f, available);
        float actionWidth = ActionControl() ? s.resetWidth : 0;
        if (auto* button = dynamic_cast<Button*>(ActionControl()))
        {
            const float caption = Host() ? Host()->MeasureText(button->Text(), 10000, false, button->Font()).width
                                         : button->Font().size * button->Text().size();
            actionWidth = std::max(actionWidth, caption + 2 * button->Style().textPadding);
        }
        // Reserve the action before applying margins. Rows without an action use that space for the value.
        actionWidth = std::clamp(actionWidth, 0.f, width);
        const float margin = std::clamp(s.resetRightMargin, 0.f, width - actionWidth);
        const float actionLeft = width - margin - actionWidth;
        const float gap = ActionControl() ? std::clamp(s.padding, 0.f, actionLeft) : 0;
        const float editorRight = actionLeft - gap;
        const float editorLeft = std::min(std::max(s.editorMinLeft, width * s.editorFraction), editorRight);
        const float nameLeft = s.treeLabelInset +
                               std::min(TreeDepth(*this) * s.treeIndent, std::max(0.f, editorLeft - s.editorMinLeft));
        return {nameLeft - s.labelPadding,
                std::max(0.f, editorLeft - nameLeft - s.padding),
                editorLeft,
                editorRight - editorLeft,
                actionLeft,
                actionWidth};
    }
    Size TreeView::Row::OnMeasure(Size available)
    {
        const auto columns = ColumnLayout(available.width);
        float height = std::max(NameControl().Measure({columns.nameWidth, available.height}).height,
                                EditorControl().Measure({columns.editorWidth, available.height}).height);
        if (auto* action = ActionControl())
            height = std::max(height, action->Measure({columns.actionWidth, available.height}).height);
        return {available.width, std::max(Style().treeRowHeight, height + 2 * Style().treeRowPadding)};
    }
    void TreeView::Row::OnArrange()
    {
        const auto b = Bounds();
        const auto columns = ColumnLayout(b.width);
        auto arrange = [&](Control& c, float x, float width)
        {
            const float height = std::min(c.DesiredSize().height, b.height);
            c.Arrange({b.x + x, b.y + (b.height - height) / 2, width, height});
        };
        arrange(NameControl(), columns.nameLeft, columns.nameWidth);
        arrange(EditorControl(), columns.editorLeft, columns.editorWidth);
        if (auto* action = ActionControl())
            arrange(*action, columns.actionLeft, columns.actionWidth);
    }
    void TreeView::Row::OnRender(Canvas& canvas)
    {
        PaintTreeRow(*this, canvas);
        Container::OnRender(canvas);
    }
    bool TreeView::Row::OnInput(const Input& input)
    {
        if (input.kind == InputKind::Down)
            return true;
        if (input.kind == InputKind::KeyDown && (input.key == LWS::KeyCode::Left || input.key == LWS::KeyCode::Right))
            return EditorControl().Dispatch(input);
        if (input.kind == InputKind::KeyDown && (input.key == LWS::KeyCode::Enter || input.key == LWS::KeyCode::Space))
        {
            if (auto* editor = FirstFocusable(EditorControl()); editor && Host() && Host()->Focus(editor))
            {
                if (dynamic_cast<Button*>(editor))
                    editor->Dispatch(input);
            }
            return true;
        }
        return false;
    }
    TreeView::Branch::Branch(std::string label) : label_(std::move(label))
    {
        spacing = 0;
        header_ = &Emplace<TreeHeading>("v  " + label_);
        content_ = &Emplace<StackPanel>();
        content_->spacing = 0;
        click_ = header_->OnClick.Connect([this] { SetExpanded(!expanded_); });
    }
    void TreeView::Branch::SetExpanded(bool value)
    {
        bool changed = expanded_ != value;
        expanded_ = value;
        content_->SetVisible(expanded_ || searching_);
        header_->SetText(std::string(expanded_ || searching_ ? "v  " : ">  ") + label_);
        Invalidate(true);
        if (changed)
            OnExpanded.Raise(expanded_);
    }
    void TreeView::Branch::RevealForSearch(bool searching)
    {
        searching_ = searching;
        content_->SetVisible(expanded_ || searching_);
        header_->SetText(std::string(expanded_ || searching_ ? "v  " : ">  ") + label_);
    }
    void TreeView::Branch::OnArrange()
    {
        auto bounds = Bounds();
        float headerHeight = header_->DesiredSize().height;
        header_->Arrange({bounds.x, bounds.y, bounds.width, headerHeight});
        if (content_->Visible())
            content_->Arrange({bounds.x, bounds.y + headerHeight, bounds.width, content_->DesiredSize().height});
    }
    bool TreeView::Branch::OnInput(const Input& input)
    {
        if (input.kind == InputKind::KeyDown && Host() && Host()->IsFocused(*header_))
        {
            if (input.key == LWS::KeyCode::Left)
            {
                SetExpanded(false);
                return true;
            }
            if (input.key == LWS::KeyCode::Right)
            {
                SetExpanded(true);
                return true;
            }
        }
        return false;
    }
    bool TreeView::OnInput(const Input& input)
    {
        if (input.kind != InputKind::KeyDown || !Host())
            return false;
        using Key = LWS::KeyCode;
        if (input.key != Key::Up && input.key != Key::Down && input.key != Key::Home && input.key != Key::End &&
            input.key != Key::Tab)
            return false;
        std::vector<Control*> rows;
        auto visit = [&](auto&& self, Control& control) -> void
        {
            if (!control.Visible())
                return;
            if (auto* branch = dynamic_cast<Branch*>(&control))
            {
                rows.push_back(&branch->Header());
                self(self, branch->Content());
            }
            else if (auto* row = dynamic_cast<Row*>(&control))
                rows.push_back(row);
            else if (auto* parent = dynamic_cast<Container*>(&control))
                for (auto& child : parent->Children())
                    self(self, *child);
        };
        visit(visit, *this);
        if (rows.empty())
            return false;
        int current = -1;
        for (size_t i = 0; i < rows.size(); ++i)
            if (Host()->HasFocusWithin(*rows[i]))
                current = int(i);
        int next = input.key == Key::Home ? 0
                   : input.key == Key::End
                       ? int(rows.size() - 1)
                       : std::clamp(current + (input.key == Key::Up || (input.key == Key::Tab && input.shift) ? -1 : 1),
                                    0, int(rows.size() - 1));
        if (Host()->Focus(rows[next]))
        {
            if (input.key == Key::Tab)
                if (auto* row = dynamic_cast<Row*>(rows[next]))
                    if (auto* text = dynamic_cast<TextBox*>(FirstFocusable(row->EditorControl())))
                        Host()->Focus(text);
            for (auto* parent = Parent(); parent; parent = parent->Parent())
                if (auto* scroll = dynamic_cast<ScrollView*>(parent))
                {
                    auto row = rows[next]->Bounds(), viewport = scroll->Bounds();
                    if (row.y < viewport.y)
                        scroll->SetOffset(scroll->Offset() + row.y - viewport.y);
                    else if (row.y + row.height > viewport.y + viewport.height)
                        scroll->SetOffset(scroll->Offset() + row.y + row.height - viewport.y - viewport.height);
                    break;
                }
        }
        return true;
    }
}  // namespace LWSUI
