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
        if (orientation == Orientation::Horizontal && count)
            childSpace.width = std::max(0.f, (available.width - spacing * float(count - 1)) / float(count));
        for (auto& child : Children())
            if (child->Visible())
            {
                auto size = child->Measure(childSpace);
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
        connection_ = bar_->OnEdit.Connect([this](double fraction, EditPhase)
                                           { SetOffset(float(fraction) * std::max(0.f, extent_ - Bounds().height)); });
    }
    void ScrollView::SetContent(std::unique_ptr<Control> content)
    {
        if (content_)
            Remove(*content_);
        content_ = content.get();
        Add(std::move(content));
        offset_ = 0;
    }
    Size ScrollView::OnMeasure(Size available)
    {
        if (content_)
            extent_ = content_
                          ->Measure({std::max(0.f, available.width - (overlayScrollBar ? 0
                                                                                       : Style().scrollbarWidth +
                                                                                             Style().scrollbarMargin)),
                                     available.height})
                          .height;
        bar_->Measure({7, available.height});
        return {available.width, std::min(extent_, available.height)};
    }
    void ScrollView::OnArrange()
    {
        auto b = Bounds();
        offset_ = std::clamp(offset_, 0.f, std::max(0.f, extent_ - b.height));
        if (content_)
            content_->Arrange(
                {b.x, b.y - offset_,
                 std::max(0.f, b.width - (overlayScrollBar ? 0 : Style().scrollbarWidth + Style().scrollbarMargin)),
                 extent_});
        bar_->SetVisible(extent_ > b.height);
        bar_->SetPage(b.height / std::max(1.f, extent_));
        bar_->SetValue(offset_ / std::max(1.f, extent_ - b.height));
        bar_->Arrange(
            {b.x + b.width - Style().scrollbarWidth - Style().scrollbarMargin, b.y, Style().scrollbarWidth, b.height});
    }
    Control* ScrollView::HitTest(float x, float y)
    {
        if (!Control::HitTest(x, y))
            return nullptr;
        if (auto* hit = bar_->HitTest(x, y))
            return hit;
        return Container::HitTest(x, y);
    }
    void ScrollView::OnRender(Canvas& canvas)
    {
        // Control::Render skips subtrees outside the active clip. Keep the complete
        // control tree and layout so scrolling preserves drafts, focus and hit testing.
        if (content_)
            content_->Render(canvas);
        bar_->Render(canvas);
    }
    void ScrollView::SetOffset(float offset)
    {
        // Reuse the ordinary layout pass for scrolling. Separate arrange-only
        // invalidation is deferred with the host's finer layout caching policy.
        const float next = std::clamp(offset, 0.f, std::max(0.f, extent_ - Bounds().height));
        if (next == offset_)
            return;
        offset_ = next;
        Invalidate(true);
    }
    bool ScrollView::OnInput(const Input& e)
    {
        if (e.kind == InputKind::Wheel)
        {
            SetOffset(offset_ - e.wheel * Style().scrollStep);
            return true;
        }
        return false;
    }

}  // namespace LWSUI
