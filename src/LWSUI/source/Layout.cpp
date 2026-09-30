#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    namespace
    {
        float Nonnegative(float value)
        {
            if (!std::isfinite(value) || value < 0)
                throw std::invalid_argument("Invalid layout dimension");
            return value;
        }
    }  // namespace
    Panel::Panel(std::unique_ptr<Control> content)
    {
        SetContent(std::move(content));
    }
    void Panel::SetContent(std::unique_ptr<Control> content)
    {
        if (content_)
            Remove(*content_);
        content_ = content ? &Add(std::move(content)) : nullptr;
        Invalidate(true);
    }
    Size Panel::OnMeasure(Size available)
    {
        const float w = padding.left + padding.right, h = padding.top + padding.bottom;
        const auto size = content_ ? content_->Measure(
                                         {std::max(0.f, available.width - w), std::max(0.f, available.height - h)})
                                   : Size{};
        return {size.width + w, std::max(minimumHeight, size.height + h)};
    }
    void Panel::OnArrange()
    {
        const auto b = Bounds();
        if (content_)
            content_->Arrange({b.x + padding.left, b.y + padding.top,
                               std::max(0.f, b.width - padding.left - padding.right),
                               std::max(0.f, b.height - padding.top - padding.bottom)});
    }
    void Panel::OnRender(Canvas& canvas)
    {
        const auto b = Bounds();
        if (paintBackground)
            canvas.Fill(b.x, b.y, b.width, b.height, Style().background);
        canvas.Fill(b.x, b.y, b.width, border.top, Style().line);
        canvas.Fill(b.x, b.y + b.height - border.bottom, b.width, border.bottom, Style().line);
        canvas.Fill(b.x, b.y, border.left, b.height, Style().line);
        canvas.Fill(b.x + b.width - border.right, b.y, border.right, b.height, Style().line);
        Container::OnRender(canvas);
    }
    Control& DockPanel::AddDocked(std::unique_ptr<Control> child, Dock dock, float extent, float maximumFraction)
    {
        extent = Nonnegative(extent);
        maximumFraction = std::clamp(Nonnegative(maximumFraction), 0.f, 1.f);
        auto& control = Add(std::move(child));
        items_.push_back({&control, dock, extent, maximumFraction});
        return control;
    }
    std::unique_ptr<Control> DockPanel::Take(Control& child)
    {
        std::erase_if(items_, [&](const auto& item) { return item.control == &child; });
        return Remove(child);
    }
    Size DockPanel::OnMeasure(Size available)
    {
        Size used{}, consumed{}, remaining = available;
        // Dock edges first without changing ownership/keyboard traversal order.
        for (bool fill : {false, true})
        for (auto& item : items_)
        {
            if ((item.dock == Dock::Fill) != fill || !item.control->Visible())
                continue;
            const auto desired = item.dock == Dock::Left || item.dock == Dock::Right
                                     ? item.control->MeasureContent(remaining)
                                     : item.control->Measure(remaining);
            if (item.dock == Dock::Top || item.dock == Dock::Bottom)
            {
                const float height = std::min(std::max(0.f, remaining.height - gap) * item.maximumFraction,
                                              item.extent > 0 ? item.extent : desired.height);
                used.width = std::max(used.width, consumed.width + desired.width);
                consumed.height += height + gap;
                used.height = std::max(used.height, consumed.height);
                remaining.height = std::max(0.f, remaining.height - height - gap);
            }
            else if (item.dock == Dock::Left || item.dock == Dock::Right)
            {
                const float width = std::min(std::max(0.f, remaining.width - gap) * item.maximumFraction,
                                             item.extent > 0 ? item.extent : desired.width);
                used.height = std::max(used.height, consumed.height + desired.height);
                consumed.width += width + gap;
                used.width = std::max(used.width, consumed.width);
                remaining.width = std::max(0.f, remaining.width - width - gap);
            }
            else
            {
                used.width = std::max(used.width, consumed.width + desired.width);
                used.height = std::max(used.height, consumed.height + desired.height);
            }
        }
        return {std::min(available.width, used.width), std::min(available.height, used.height)};
    }
    void DockPanel::OnArrange()
    {
        auto b = Bounds();
        // Dock edges first without changing ownership/keyboard traversal order.
        for (bool fill : {false, true})
        for (auto& item : items_)
        {
            if ((item.dock == Dock::Fill) != fill || !item.control->Visible())
                continue;
            const auto desired = item.control->DesiredSize();
            auto r = b;
            if (item.dock == Dock::Top || item.dock == Dock::Bottom)
            {
                r.height = std::min(std::max(0.f, b.height - gap) * item.maximumFraction,
                                    item.extent > 0 ? item.extent : desired.height);
                if (item.dock == Dock::Bottom)
                    r.y += b.height - r.height;
                else
                    b.y += r.height + gap;
                b.height = std::max(0.f, b.height - r.height - gap);
            }
            else if (item.dock == Dock::Left || item.dock == Dock::Right)
            {
                r.width = std::min(std::max(0.f, b.width - gap) * item.maximumFraction,
                                   item.extent > 0 ? item.extent : desired.width);
                if (item.dock == Dock::Right)
                    r.x += b.width - r.width;
                else
                    b.x += r.width + gap;
                b.width = std::max(0.f, b.width - r.width - gap);
            }
            item.control->Arrange(r);
        }
    }
    Control& FlowPanel::AddItem(std::unique_ptr<Control> child, bool grow, float minimumWidth)
    {
        minimumWidth = Nonnegative(minimumWidth);
        auto& control = Add(std::move(child));
        items_.push_back({&control, grow, minimumWidth});
        return control;
    }
    Size FlowPanel::OnMeasure(Size available)
    {
        rows_.clear();
        for (size_t i = 0; i < items_.size(); ++i)
        {
            auto& item = items_[i];
            if (!item.control->Visible())
                continue;
            const auto measured = item.grow ? item.control->Measure({item.minimumWidth, available.height})
                                            : item.control->MeasureContent({available.width, available.height});
            const float width = item.width = std::min(available.width,
                                                      std::max({minimumItemWidth, item.minimumWidth, measured.width}));
            if (rows_.empty() || (!rows_.back().items.empty() && rows_.back().width + gap + width > available.width))
                rows_.push_back({});
            auto& row = rows_.back();
            if (!row.items.empty())
                row.width += gap;
            row.width += width;
            row.height = std::max(row.height, measured.height);
            row.items.push_back(i);
        }
        float height = 0, width = 0;
        for (auto& row : rows_)
        {
            size_t count = 0;
            for (auto i : row.items)
                count += items_[i].grow;
            row.height = 0;
            for (auto i : row.items)
            {
                auto& item = items_[i];
                if (item.grow && count)
                    item.width += std::max(0.f, available.width - row.width) / count;
                row.height = std::max(row.height, item.control->Measure({item.width, available.height}).height);
            }
            height += row.height;
            width = std::max(width, count ? available.width : row.width);
        }
        if (!rows_.empty())
            height += (rows_.size() - 1) * rowGap;
        return {width, std::max(minimumHeight, height)};
    }
    void FlowPanel::OnArrange()
    {
        const auto b = Bounds();
        float y = b.y;
        for (const auto& row : rows_)
        {
            const float free = std::max(0.f, b.width - row.width);
            size_t growing = 0;
            for (auto i : row.items)
                growing += items_[i].grow;
            float x = b.x + (alignEnd && !growing ? free : 0);
            const float height = std::min(row.height, std::max(0.f, b.y + b.height - y));
            for (size_t n = 0; n < row.items.size(); ++n)
            {
                const auto i = row.items[n];
                auto& item = items_[i];
                if (firstLeading && i == 0 && !growing)
                    x = b.x;
                else if (firstLeading && n == 1 && row.items[0] == 0 && !growing)
                    x += free;
                const float width = item.width;
                item.control->Arrange({x, y, width, height});
                x += width + gap;
            }
            y += row.height + rowGap;
        }
    }
    class SplitPanel::Grip final : public Control
    {
      public:

        explicit Grip(SplitPanel& owner) : owner_(owner) {}
        bool Focusable() const override { return true; }
        void Grab() { Capture(); }
        void Release() { ReleaseCapture(); }

      protected:

        void OnRender(Canvas& c) override
        {
            const auto b = Bounds();
            const bool active = owner_.dragging_ || Hovered() || (Host() && Host()->IsFocused(*this));
            c.Fill(b.x, b.y, b.width, b.height, active ? Style().accent : Style().line);
        }
        bool OnInput(const Input& input) override
        {
            const bool horizontal = owner_.orientation_ == Orientation::Horizontal;
            const float position = horizontal ? input.x : input.y;
            if (input.kind == InputKind::Down && input.button == LWS::MouseButton::Left)
            {
                if (input.clickCount == 2)
                    owner_.Reset();
                else
                    owner_.Begin(position);
                return true;
            }
            if (input.kind == InputKind::Move && owner_.dragging_)
            {
                owner_.Move(position - owner_.start_, EditPhase::Preview);
                return true;
            }
            if (input.kind == InputKind::Up && owner_.dragging_)
            {
                owner_.Complete(EditPhase::Commit);
                return true;
            }
            if (input.kind == InputKind::Cancel || input.kind == InputKind::Blur)
            {
                owner_.CancelDrag();
                return true;
            }
            if (input.kind != InputKind::KeyDown)
                return false;
            if (input.key == LWS::KeyCode::Escape)
            {
                owner_.CancelDrag();
                return true;
            }
            if (input.key == LWS::KeyCode::Home)
            {
                owner_.Reset();
                return true;
            }
            const auto back = horizontal ? LWS::KeyCode::Left : LWS::KeyCode::Up;
            const auto forward = horizontal ? LWS::KeyCode::Right : LWS::KeyCode::Down;
            if (input.key != back && input.key != forward)
                return false;
            owner_.startFirst_ = owner_.firstSize_;
            owner_.Move(input.key == back ? -10 : 10, EditPhase::Commit);
            return true;
        }

      private:

        SplitPanel& owner_;
    };
    SplitPanel::SplitPanel(std::unique_ptr<Control> first, std::unique_ptr<Control> second, Orientation orientation)
        : orientation_(orientation)
    {
        if (!first || !second)
            throw std::invalid_argument("SplitPanel requires two panes");
        first_ = &Add(std::move(first));
        second_ = &Add(std::move(second));
        grip_ = &Emplace<Grip>(*this);
        grip_->cursor = orientation == Orientation::Horizontal ? LWS::CursorShape::SizeEW : LWS::CursorShape::SizeNS;
    }
    SplitPanel::~SplitPanel()
    {
        CancelDrag();
    }
    Control& SplitPanel::Divider() const
    {
        return *grip_;
    }
    float SplitPanel::Extent() const
    {
        return std::max(0.f,
                        (orientation_ == Orientation::Horizontal ? Bounds().width : Bounds().height) - dividerSize_);
    }
    void SplitPanel::SetOrientation(Orientation value)
    {
        if (orientation_ == value)
            return;
        CancelDrag();
        orientation_ = value;
        grip_->cursor = value == Orientation::Horizontal ? LWS::CursorShape::SizeEW : LWS::CursorShape::SizeNS;
        Invalidate(true);
    }
    void SplitPanel::SetRatio(float ratio)
    {
        if (!std::isfinite(ratio))
            throw std::invalid_argument("Invalid split ratio");
        ratio = std::clamp(ratio, 0.f, 1.f);
        if (!trailing_ && ratio_ == ratio && defaultRatio_ == ratio)
            return;
        CancelDrag();
        trailing_ = false;
        ratio_ = defaultRatio_ = ratio;
        adjusted_ = false;
        Invalidate(true);
    }
    void SplitPanel::SetDefaultTrailingSize(float size, float maximumFraction)
    {
        size = Nonnegative(size);
        maximumFraction = std::clamp(Nonnegative(maximumFraction), 0.f, 1.f);
        if (trailing_ && defaultTrailingSize_ == size && trailingLimit_ == maximumFraction)
            return;
        trailingLimit_ = maximumFraction;
        CancelDrag();
        trailing_ = true;
        defaultTrailingSize_ = size;
        if (!adjusted_)
            trailingSize_ = size;
        Invalidate(true);
    }
    void SplitPanel::SetMinimumSizes(float first, float second)
    {
        first = Nonnegative(first);
        second = Nonnegative(second);
        if (firstMinimum_ == first && secondMinimum_ == second)
            return;
        CancelDrag();
        firstMinimum_ = first;
        secondMinimum_ = second;
        Invalidate(true);
    }
    void SplitPanel::SetDividerSize(float value)
    {
        value = Nonnegative(value);
        if (dividerSize_ == value)
            return;
        CancelDrag();
        dividerSize_ = value;
        Invalidate(true);
    }
    void SplitPanel::OnArrange()
    {
        const auto b = Bounds();
        if (dragging_ && (arrangedSize_.width != b.width || arrangedSize_.height != b.height))
            CancelDrag();
        arrangedSize_ = {b.width, b.height};
        const bool horizontal = orientation_ == Orientation::Horizontal;
        const float extent = Extent(), total = firstMinimum_ + secondMinimum_;
        const float factor = total > extent && total > 0 ? extent / total : 1;
        firstSize_ = std::clamp(
            trailing_ ? extent - (adjusted_ ? trailingSize_ : std::min(trailingSize_, extent * trailingLimit_))
                      : extent * ratio_,
            firstMinimum_ * factor, std::max(firstMinimum_ * factor, extent - secondMinimum_ * factor));
        const float divider = std::min(dividerSize_, horizontal ? b.width : b.height);
        const auto rect = [&](float start, float length)
        { return horizontal ? Rect{b.x + start, b.y, length, b.height} : Rect{b.x, b.y + start, b.width, length}; };
        const auto first = rect(0, firstSize_), second = rect(firstSize_ + divider, extent - firstSize_);
        first_->Measure({first.width, first.height});
        first_->Arrange(first);
        second_->Measure({second.width, second.height});
        second_->Arrange(second);
        grip_->Arrange(rect(firstSize_, divider));
    }
    bool SplitPanel::OnPreviewInput(const Input& input)
    {
        if (input.kind == InputKind::Down && grip_->Bounds().Contains(input.x, input.y))
            return grip_->Dispatch(input);
        if (dragging_ && input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Escape)
        {
            CancelDrag();
            return true;
        }
        return Container::OnPreviewInput(input);
    }
    void SplitPanel::Begin(float position)
    {
        CancelDrag();
        dragging_ = true;
        savedRatio_ = ratio_;
        savedTrailing_ = trailingSize_;
        savedAdjusted_ = adjusted_;
        start_ = position;
        startFirst_ = firstSize_;
        grip_->Grab();
        Invalidate();
    }
    void SplitPanel::Move(float delta, EditPhase phase)
    {
        const float extent = Extent();
        if (extent <= 0)
            return;
        const float total = firstMinimum_ + secondMinimum_, factor = total > extent && total > 0 ? extent / total : 1;
        const float first = std::clamp(startFirst_ + delta, firstMinimum_ * factor,
                                       std::max(firstMinimum_ * factor, extent - secondMinimum_ * factor));
        ratio_ = first / extent;
        trailingSize_ = extent - first;
        adjusted_ = true;
        Invalidate(true);
        OnEdit.Raise(ratio_, phase);
    }
    void SplitPanel::Complete(EditPhase phase)
    {
        if (!dragging_)
            return;
        dragging_ = false;
        grip_->Release();
        Invalidate();
        OnEdit.Raise(ratio_, phase);
    }
    void SplitPanel::CancelDrag()
    {
        if (!dragging_)
            return;
        ratio_ = savedRatio_;
        trailingSize_ = savedTrailing_;
        adjusted_ = savedAdjusted_;
        Complete(EditPhase::Cancel);
        Invalidate(true);
    }
    bool SplitPanel::Finish(EditPhase phase)
    {
        if (phase == EditPhase::Cancel)
            CancelDrag();
        else
            Complete(EditPhase::Commit);
        return Container::Finish(phase);
    }
    void SplitPanel::Reset()
    {
        CancelDrag();
        adjusted_ = false;
        ratio_ = defaultRatio_;
        trailingSize_ = defaultTrailingSize_;
        Invalidate(true);
        OnEdit.Raise(ratio_, EditPhase::Commit);
    }
}  // namespace LWSUI
