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
        bool Dragging() const;
        ScrollView& Pane(size_t index) const;
        Control& Divider(size_t index) const;
        Control& PaneContainer(size_t index) const;
        std::array<float, 3> Proportions() const;
        std::array<float, 2> CrossProportions() const;

      protected:

        Size OnMeasure(Size available) override;
        void OnArrange() override;
        void OnDetach() override { CancelDrag(); }

      private:

        class PaneFrame;
        std::array<PaneFrame*, 5> panes_{};
        std::array<SplitPanel*, 4> splits_{};
        DockPanel* layout_;
        StackPanel* toolbar_;
        Button* orientation_;
        Label* explanation_;
        Event<void()>::Connection orientationClick_, resetClick_;
        bool preferredSideBySide_ = false, sideBySide_ = false;
    };
}  // namespace LWSUI::demo
