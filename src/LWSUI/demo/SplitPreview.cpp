#include "SplitPreview.hpp"
#include <LWSUI/UIHost.hpp>
#include <algorithm>
#include <cmath>
#include <numeric>

namespace LWSUI::demo
{
    namespace
    {
        constexpr float gap = 8;
        constexpr std::array<float, 3> stackedMinimum{150, 90, 112};
        constexpr std::array<float, 3> horizontalMinimum{180, 140, 160};
        constexpr float sideThreshold = 180 + 140 + 160 + 2 * gap;
        std::array<float, 3> Allocate(float available, std::array<float, 3> weights, std::array<float, 3> minimum)
        {
            const float required = std::accumulate(minimum.begin(), minimum.end(), 0.f);
            if (available <= required)
            {
                for (auto& size : minimum)
                    size *= available / required;
                return minimum;
            }
            std::array<float, 3> sizes{};
            std::array<bool, 3> fixed{};
            for (int pass = 0; pass < 3; ++pass)
            {
                float left = available, weight = 0;
                for (size_t i = 0; i < 3; ++i)
                    if (fixed[i])
                        left -= sizes[i];
                    else
                        weight += weights[i];
                bool limited = false;
                for (size_t i = 0; i < 3; ++i)
                    if (!fixed[i])
                    {
                        sizes[i] = left * weights[i] / weight;
                        if (sizes[i] < minimum[i])
                        {
                            sizes[i] = minimum[i];
                            fixed[i] = true;
                            limited = true;
                        }
                    }
                if (!limited)
                    break;
            }
            return sizes;
        }
    }  // namespace
    namespace
    {
        // Pane captions use their text height instead of the general control-row minimum.
        class PaneHeading final : public Label
        {
          protected:

            Size OnMeasure(Size available) override
            {
                return {available.width,
                        Host()->MeasureText(Text(), std::max(1.f, available.width), true, Font()).height};
            }
            void OnRender(Canvas& canvas) override
            {
                const auto b = Bounds();
                DrawText(canvas, Text(), b.x, b.y, b.width, b.height, Foreground(), true);
            }
        };
    }  // namespace
    class SplitPreview::PaneFrame final : public Container
    {
      public:

        PaneFrame(std::string title, std::unique_ptr<Control> content) : title_(std::move(title))
        {
            heading_ = &Emplace<PaneHeading>();
            heading_->wrap = true;
            scroll_ = &Emplace<ScrollView>();
            scroll_->SetContent(std::move(content));
        }
        ScrollView& Scroll() const { return *scroll_; }

      protected:

        Size OnMeasure(Size available) override { return available; }
        void OnArrange() override
        {
            const auto b = Bounds();
            heading_->fontRole = b.height < 96 ? FontRole::Small : FontRole::Normal;
            heading_->SetText(title_ + " / " + std::to_string(int(b.width)) + " x " + std::to_string(int(b.height)));
            const float width = std::max(0.f, b.width - 12);
            const auto title = heading_->Measure({width, b.height});
            heading_->Arrange({b.x + 6, b.y + 3, width, title.height});
            const Rect viewport{b.x + 6, b.y + title.height + 6, width, std::max(0.f, b.height - title.height - 9)};
            scroll_->Measure({viewport.width, viewport.height});
            scroll_->Arrange(viewport);
        }
        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, Style().background);
            canvas.Line(b.x, b.y, b.x + b.width, b.y, 1, Style().line);
            canvas.Line(b.x, b.y, b.x, b.y + b.height, 1, Style().line);
            canvas.Line(b.x + b.width - 1, b.y, b.x + b.width - 1, b.y + b.height, 1, Style().line);
            canvas.Line(b.x, b.y + b.height - 1, b.x + b.width, b.y + b.height - 1, 1, Style().line);
            Container::OnRender(canvas);
        }

      private:

        std::string title_;
        Label* heading_;
        ScrollView* scroll_;
    };
    class SplitPreview::Grip final : public Control
    {
      public:

        Grip(SplitPreview& owner, size_t index) : owner_(owner), index_(index) {}
        bool Focusable() const override { return true; }
        void Grab() { Capture(); }
        void Release() { ReleaseCapture(); }

      protected:

        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            const bool active = owner_.active_ == index_ || (Host() && Host()->IsFocused(*this));
            canvas.Fill(b.x, b.y, b.width, b.height, active ? Style().accent : Style().line);
            const auto ink = active ? Style().selectionForeground : Style().foreground;
            for (int i = -1; i <= 1; ++i)
                if (owner_.MovesHorizontally(index_))
                    canvas.Line(b.x + 2, b.y + b.height / 2 + i * 4, b.x + b.width - 2, b.y + b.height / 2 + i * 4, 1,
                                ink);
                else
                    canvas.Line(b.x + b.width / 2 + i * 4, b.y + 2, b.x + b.width / 2 + i * 4, b.y + b.height - 2, 1,
                                ink);
        }
        bool OnInput(const Input& input) override
        {
            const float coordinate = owner_.MovesHorizontally(index_) ? input.x : input.y;
            if (input.kind == InputKind::Down && input.button == LWS::MouseButton::Left)
            {
                owner_.BeginDrag(index_, coordinate);
                return true;
            }
            if (input.kind == InputKind::Move && owner_.active_ == index_)
            {
                owner_.MoveDrag(coordinate);
                return true;
            }
            if (input.kind == InputKind::Up && owner_.active_ == index_)
            {
                owner_.CommitDrag();
                return true;
            }
            if (input.kind == InputKind::Cancel || input.kind == InputKind::Blur)
            {
                owner_.CancelDrag();
                return true;
            }
            if (input.kind == InputKind::Focus)
            {
                Invalidate();
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
                owner_.ResetSplit();
                return true;
            }
            const auto backward = owner_.MovesHorizontally(index_) ? LWS::KeyCode::Left : LWS::KeyCode::Up;
            const auto forward = owner_.MovesHorizontally(index_) ? LWS::KeyCode::Right : LWS::KeyCode::Down;
            if (input.key != backward && input.key != forward)
                return false;
            owner_.MoveDivider(index_, input.key == backward ? -10.f : 10.f);
            return true;
        }

      private:

        SplitPreview& owner_;
        size_t index_;
    };
    SplitPreview::SplitPreview(std::array<std::unique_ptr<Control>, 5> contents)
    {
        toolbar_ = &Emplace<StackPanel>();
        toolbar_->orientation = Orientation::Horizontal;
        orientation_ = &toolbar_->Emplace<Button>("Rotate layout");
        auto& reset = toolbar_->Emplace<Button>("Reset Split");
        orientationClick_ = orientation_->OnClick.Connect([this] { SetSideBySide(!preferredSideBySide_); });
        resetClick_ = reset.OnClick.Connect([this] { ResetSplit(); });
        explanation_ = &Emplace<Label>();
        explanation_->wrap = true;
        constexpr std::array titles{"Drawing", "Details", "Quick Controls", "Inspector", "Activity"};
        for (size_t i = 0; i < panes_.size(); ++i)
            panes_[i] = &Emplace<PaneFrame>(titles[i], std::move(contents[i]));
        for (size_t i = 0; i < grips_.size(); ++i)
            grips_[i] = &Emplace<Grip>(*this, i);
    }
    SplitPreview::~SplitPreview()
    {
        CancelDrag();
    }
    ScrollView& SplitPreview::Pane(size_t index) const
    {
        return panes_.at(index)->Scroll();
    }
    Control& SplitPreview::Divider(size_t index) const
    {
        return *grips_.at(index);
    }
    void SplitPreview::SetSideBySide(bool value)
    {
        if (preferredSideBySide_ == value)
            return;
        CancelDrag();
        preferredSideBySide_ = value;
        orientation_->SetText(value ? "Default layout" : "Rotate layout");
        Invalidate(true);
    }
    void SplitPreview::ResetSplit()
    {
        CancelDrag();
        proportions_ = {.5f, .3f, .2f};
        crossProportions_ = {.65f, .65f};
        Invalidate(true);
    }
    Size SplitPreview::OnMeasure(Size available)
    {
        return available;
    }
    std::array<float, 3> SplitPreview::Minimums() const
    {
        auto minimum = sideBySide_ ? horizontalMinimum : stackedMinimum;
        const float available = std::max(0.f, (sideBySide_ ? area_.width : area_.height) - 2 * gap);
        const float sum = std::accumulate(minimum.begin(), minimum.end(), 0.f);
        if (available < sum)
            for (auto& size : minimum)
                size *= available / sum;
        return minimum;
    }
    std::array<float, 2> SplitPreview::CrossMinimums(size_t group) const
    {
        std::array<float, 2> minimum = sideBySide_ ? (group == 0 ? std::array<float, 2>{150, 100}
                                                                 : std::array<float, 2>{112, 90})
                                                   : (group == 0 ? std::array<float, 2>{180, 140}
                                                                 : std::array<float, 2>{160, 140});
        const float available = std::max(0.f, (sideBySide_ ? area_.height : area_.width) - gap);
        const float sum = minimum[0] + minimum[1];
        if (available < sum)
            for (auto& size : minimum)
                size *= available / sum;
        return minimum;
    }
    void SplitPreview::OnArrange()
    {
        const auto b = Bounds();
        const bool side = preferredSideBySide_ && b.width >= sideThreshold;
        explanation_->SetText(
            preferredSideBySide_ && !side ? "Five containers: one shared window. Widen preview to rotate."
            : side ? "Five containers: one shared window. Rotated layout preserves the same shared edges."
                   : "Five containers: one shared window. Inspector spans Drawing + Details; Activity sits beside "
                     "Quick Controls.");
        const auto toolbar = toolbar_->Measure({b.width, b.height});
        const auto note = explanation_->Measure({b.width, b.height});
        toolbar_->Arrange({b.x, b.y, b.width, toolbar.height});
        explanation_->Arrange({b.x, b.y + toolbar.height + 6, b.width, note.height});
        const float top = toolbar.height + note.height + 12;
        const Rect next{b.x, b.y + top, b.width, std::max(0.f, b.height - top)};
        if (side != sideBySide_ || next.width != area_.width || next.height != area_.height)
            CancelDrag();
        sideBySide_ = side;
        area_ = next;
        const float available = std::max(0.f, (side ? area_.width : area_.height) - 2 * gap);
        sizes_ = Allocate(available, proportions_, Minimums());
        const float cross = side ? area_.height : area_.width;
        const float room = std::max(0.f, cross - gap);
        for (size_t group = 0; group < 2; ++group)
        {
            const auto minimum = CrossMinimums(group);
            crossSizes_[group] = std::clamp(room * crossProportions_[group], minimum[0],
                                            std::max(minimum[0], room - minimum[1]));
        }
        // Work in primary/cross coordinates, then transpose for the alternate layout.
        const auto rectangle = [&](float primary, float secondary, float length, float width)
        {
            return side ? Rect{area_.x + primary, area_.y + secondary, length, width}
                        : Rect{area_.x + secondary, area_.y + primary, width, length};
        };
        const float upper = sizes_[0] + gap + sizes_[1];
        const float lower = upper + gap;
        const std::array<Rect, 5> bounds{rectangle(0, 0, sizes_[0], crossSizes_[0]),
                                         rectangle(sizes_[0] + gap, 0, sizes_[1], crossSizes_[0]),
                                         rectangle(lower, 0, sizes_[2], crossSizes_[1]),
                                         rectangle(0, crossSizes_[0] + gap, upper, room - crossSizes_[0]),
                                         rectangle(lower, crossSizes_[1] + gap, sizes_[2], room - crossSizes_[1])};
        for (size_t i = 0; i < panes_.size(); ++i)
        {
            panes_[i]->Measure({bounds[i].width, bounds[i].height});
            panes_[i]->Arrange(bounds[i]);
        }
        grips_[0]->Arrange(rectangle(sizes_[0], 0, gap, crossSizes_[0]));
        grips_[1]->Arrange(rectangle(upper, 0, gap, cross));
        grips_[2]->Arrange(rectangle(0, crossSizes_[0], upper, gap));
        grips_[3]->Arrange(rectangle(lower, crossSizes_[1], sizes_[2], gap));
    }
    bool SplitPreview::OnPreviewInput(const Input& input)
    {
        // Pointer resizing must not transfer keyboard focus or commit a draft in either pane.
        if (input.kind == InputKind::Down)
            for (auto* grip : grips_)
                if (grip->Bounds().Contains(input.x, input.y))
                    return grip->Dispatch(input);
        if (active_ && input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Escape)
        {
            CancelDrag();
            return true;
        }
        return false;
    }
    void SplitPreview::BeginDrag(size_t index, float coordinate)
    {
        CancelDrag();
        active_ = index;
        saved_ = proportions_;
        savedCross_ = crossProportions_;
        dragCross_ = crossSizes_;
        dragSizes_ = sizes_;
        start_ = coordinate;
        grips_[index]->Grab();
        SetCursor(MovesHorizontally(index) ? LWS::CursorShape::SizeEW : LWS::CursorShape::SizeNS);
        Invalidate();
    }
    void SplitPreview::MoveDivider(size_t index, float delta)
    {
        if (index >= 2)
        {
            const size_t group = index - 2;
            const float room = std::max(0.f, (sideBySide_ ? area_.height : area_.width) - gap);
            const auto minimum = CrossMinimums(group);
            crossSizes_[group] = std::clamp(crossSizes_[group] + delta, minimum[0],
                                            std::max(minimum[0], room - minimum[1]));
            if (room > 0)
                crossProportions_[group] = crossSizes_[group] / room;
            Invalidate(true);
            return;
        }
        const auto minimum = Minimums();
        const float sum = sizes_[index] + sizes_[index + 1];
        const float first = std::clamp(sizes_[index] + delta, minimum[index],
                                       std::max(minimum[index], sum - minimum[index + 1]));
        sizes_[index] = first;
        sizes_[index + 1] = sum - first;
        const float total = std::accumulate(sizes_.begin(), sizes_.end(), 0.f);
        if (total > 0)
            for (size_t i = 0; i < 3; ++i)
                proportions_[i] = sizes_[i] / total;
        Invalidate(true);
    }
    void SplitPreview::MoveDrag(float coordinate)
    {
        if (!active_)
            return;
        sizes_ = dragSizes_;
        crossSizes_ = dragCross_;
        MoveDivider(*active_, coordinate - start_);
    }
    void SplitPreview::CommitDrag()
    {
        const auto active = active_;
        active_.reset();
        if (active)
            grips_[*active]->Release();
        SetCursor(LWS::CursorShape::Arrow);
        Invalidate();
    }
    void SplitPreview::CancelDrag()
    {
        if (!active_)
            return;
        proportions_ = saved_;
        crossProportions_ = savedCross_;
        CommitDrag();
        Invalidate(true);
    }
    void SplitPreview::SetCursor(LWS::CursorShape shape)
    {
        if (shape == cursor_ || !Host())
            return;
        cursor_ = shape;
        const auto result = Host()->Window().SetMouseCursor(LWS::Cursor::FromShape(shape));
        if (result != LWS::Result::Success)
            Host()->ReportError("Cannot set divider cursor");
    }
    void SplitPreview::PointerCursor(std::optional<LWS::Point> point)
    {
        auto over = active_;
        if (point && !over)
            for (size_t i = 0; i < grips_.size(); ++i)
                if (grips_[i]->Bounds().Contains(float(point->x), float(point->y)))
                {
                    over = i;
                    break;
                }
        SetCursor(over ? (MovesHorizontally(*over) ? LWS::CursorShape::SizeEW : LWS::CursorShape::SizeNS)
                       : LWS::CursorShape::Arrow);
    }
}  // namespace LWSUI::demo
