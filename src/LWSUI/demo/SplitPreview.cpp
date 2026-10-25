#include "SplitPreview.hpp"
#include <LWSUI/UIHost.hpp>
namespace LWSUI::demo
{
    class SplitPreview::PaneFrame final : public Panel
    {
      public:
        PaneFrame(std::string title, std::unique_ptr<Control> content) : title_(std::move(title))
        {
            padding = Insets{6, 3, 6, 6};
            border = Insets{1};
            auto dock = std::make_unique<DockPanel>();
            dock->gap = 3;
            auto heading = std::make_unique<Label>(title_);
            heading_ = heading.get();
            heading_->wrap = true;
            dock->AddDocked(std::move(heading), Dock::Top);
            auto scroll = std::make_unique<ScrollView>();
            scroll_ = scroll.get();
            scroll_->SetContent(std::move(content));
            dock->AddDocked(std::move(scroll), Dock::Fill);
            SetContent(std::move(dock));
        }
        ScrollView& Scroll() const { return *scroll_; }
      protected:
        void OnArrange() override
        {
            const auto b = Bounds();
            heading_->SetText(title_ + " / " + std::to_string(int(b.width)) + " x " + std::to_string(int(b.height)));
            Panel::OnMeasure({b.width, b.height});
            Panel::OnArrange();
        }
      private:
        std::string title_;
        Label* heading_;
        ScrollView* scroll_;
    };
    SplitPreview::SplitPreview(std::array<std::unique_ptr<Control>, 5> contents)
    {
        layout_ = &Emplace<DockPanel>();
        layout_->gap = 6;
        auto toolbar = std::make_unique<StackPanel>();
        toolbar_ = toolbar.get();
        toolbar_->orientation = Orientation::Horizontal;
        orientation_ = &toolbar_->Emplace<Button>("Rotate layout");
        auto& reset = toolbar_->Emplace<Button>("Reset Split");
        orientationClick_ = orientation_->OnClick.Connect([this] { SetSideBySide(!preferredSideBySide_); });
        resetClick_ = reset.OnClick.Connect([this] { ResetSplit(); });
        layout_->AddDocked(std::move(toolbar), Dock::Top);
        auto explanation = std::make_unique<Label>();
        explanation_ = explanation.get();
        explanation_->wrap = true;
        layout_->AddDocked(std::move(explanation), Dock::Top);
        constexpr std::array titles{"Drawing", "Details", "Quick Controls", "Inspector", "Activity"};
        std::array<std::unique_ptr<Control>, 5> panes;
        for (size_t i = 0; i < panes.size(); ++i)
        {
            auto pane = std::make_unique<PaneFrame>(titles[i], std::move(contents[i]));
            panes_[i] = pane.get();
            panes[i] = std::move(pane);
        }
        auto upperLeft = std::make_unique<SplitPanel>(std::move(panes[0]), std::move(panes[1]));
        splits_[0] = upperLeft.get();
        auto upper = std::make_unique<SplitPanel>(std::move(upperLeft), std::move(panes[3]), Orientation::Horizontal);
        splits_[2] = upper.get();
        auto lower = std::make_unique<SplitPanel>(std::move(panes[2]), std::move(panes[4]), Orientation::Horizontal);
        splits_[3] = lower.get();
        auto root = std::make_unique<SplitPanel>(std::move(upper), std::move(lower));
        splits_[1] = root.get();
        layout_->AddDocked(std::move(root), Dock::Fill);
        ResetSplit();
    }
    SplitPreview::~SplitPreview()
    {
        CancelDrag();
    }
    ScrollView& SplitPreview::Pane(size_t index) const
    {
        return panes_.at(index)->Scroll();
    }
    Control& SplitPreview::PaneContainer(size_t index) const
    {
        return *panes_.at(index);
    }
    Control& SplitPreview::Divider(size_t index) const
    {
        return splits_.at(index)->Divider();
    }
    bool SplitPreview::Dragging() const
    {
        return std::ranges::any_of(splits_, [](const auto* split) { return split->Dragging(); });
    }
    void SplitPreview::CancelDrag()
    {
        for (auto* split : splits_)
            split->CancelDrag();
    }
    std::array<float, 3> SplitPreview::Proportions() const
    {
        const float upper = splits_[1]->Ratio(), drawing = splits_[0]->Ratio();
        return {upper * drawing, upper * (1 - drawing), 1 - upper};
    }
    std::array<float, 2> SplitPreview::CrossProportions() const
    {
        return {splits_[2]->Ratio(), splits_[3]->Ratio()};
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
        splits_[0]->SetRatio(.625f);
        splits_[1]->SetRatio(.8f);
        splits_[2]->SetRatio(.65f);
        splits_[3]->SetRatio(.65f);
        Invalidate(true);
    }
    Size SplitPreview::OnMeasure(Size available)
    {
        const bool side = preferredSideBySide_ && available.width >= 496;
        sideBySide_ = side;
        explanation_->SetText(
            preferredSideBySide_ && !side ? "Five containers: one shared window. Widen preview to rotate."
            : side ? "Five containers: one shared window. Rotated layout preserves the same shared edges."
                   : "Five containers: one shared window. Inspector spans Drawing + Details; Activity sits beside "
                     "Quick Controls.");
        for (size_t i = 0; i < splits_.size(); ++i)
            splits_[i]->SetOrientation((i < 2 ? side : !side) ? Orientation::Horizontal : Orientation::Vertical);
        splits_[0]->SetMinimumSizes(side ? 180 : 150, side ? 140 : 90);
        splits_[1]->SetMinimumSizes(side ? 328 : 248, side ? 160 : 112);
        splits_[2]->SetMinimumSizes(side ? 150 : 180, side ? 100 : 140);
        splits_[3]->SetMinimumSizes(side ? 112 : 160, side ? 90 : 140);
        layout_->Measure(available);
        return available;
    }
    void SplitPreview::OnArrange()
    {
        layout_->Arrange(Bounds());
    }
}  // namespace LWSUI::demo
