#pragma once
#include <LWSUI/Primitives.hpp>
namespace LWSUI
{
    enum class Orientation
    {
        Vertical,
        Horizontal
    };
    class StackPanel : public Container
    {
      public:

        Orientation orientation = Orientation::Vertical;
        float spacing = 6;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;
    };
    // Positive columns are fixed logical widths; negative columns are proportional weights.
    class Grid : public Container
    {
      public:

        std::vector<float> columns{-1};
        float spacing = 6;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;

      private:

        std::vector<float> rowHeights_;
    };
    class ScrollView : public Container
    {
      public:

        ScrollView();
        void SetContent(std::unique_ptr<Control> content);
        Control* Content() const { return content_; }
        void SetOffset(float offset);
        float Offset() const { return offset_; }
        bool overlayScrollBar = false;
        Control* HitTest(float x, float y) override;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;
        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;

      private:

        // Child roles are owned by this composite.
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;

        Control* content_ = nullptr;
        ScrollBar* bar_;
        float offset_ = 0, extent_ = 0;
        LWSUI::Event<void(double, EditPhase)>::Connection connection_;
    };

}  // namespace LWSUI
