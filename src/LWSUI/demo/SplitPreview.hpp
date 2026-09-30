#pragma once
#include <LWSUI/Containers.hpp>
#include <LWS/CursorShape.hpp>
#include <array>
#include <optional>

namespace LWSUI::demo
{
    // Five ordinary control trees sharing their parent's host; never creates native windows.
    class SplitPreview final : public Container
    {
      public:

        explicit SplitPreview(std::array<std::unique_ptr<Control>, 5> contents);
        ~SplitPreview();
        void SetSideBySide(bool value);
        bool PrefersSideBySide() const { return preferredSideBySide_; }
        bool IsSideBySide() const { return sideBySide_; }
        void ResetSplit();
        void CancelDrag();
        bool Dragging() const { return active_.has_value(); }
        void PointerCursor(std::optional<LWS::Point> position);
        ScrollView& Pane(size_t index) const;
        Control& Divider(size_t index) const;
        std::array<float, 3> Proportions() const { return proportions_; }
        std::array<float, 2> CrossProportions() const { return crossProportions_; }

      protected:

        Size OnMeasure(Size available) override;
        void OnArrange() override;
        bool OnPreviewInput(const Input& input) override;
        void OnDetach() override { CancelDrag(); }

      private:

        class PaneFrame;
        class Grip;
        friend class Grip;
        void BeginDrag(size_t index, float position);
        void MoveDrag(float position);
        void CommitDrag();
        void MoveDivider(size_t index, float delta);
        void SetCursor(LWS::CursorShape shape);
        std::array<float, 3> Minimums() const;
        std::array<float, 2> CrossMinimums(size_t group) const;
        bool MovesHorizontally(size_t index) const { return index < 2 ? sideBySide_ : !sideBySide_; }
        std::array<PaneFrame*, 5> panes_{};
        std::array<Grip*, 4> grips_{};
        StackPanel* toolbar_;
        Button* orientation_;
        Label* explanation_;
        Event<void()>::Connection orientationClick_, resetClick_;
        std::array<float, 3> proportions_{.5f, .3f, .2f}, sizes_{}, saved_{}, dragSizes_{};
        std::array<float, 2> crossProportions_{.65f, .65f}, savedCross_{}, dragCross_{}, crossSizes_{};
        std::optional<size_t> active_;
        float start_ = 0;
        Rect area_{};
        bool preferredSideBySide_ = false, sideBySide_ = false;
        LWS::CursorShape cursor_ = LWS::CursorShape::Arrow;
    };
}  // namespace LWSUI::demo
