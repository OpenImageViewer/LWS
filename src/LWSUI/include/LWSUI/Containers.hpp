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
    class TreeView : public StackPanel
    {
      public:

        TreeView() { spacing = 0; }
        // Property rows share aligned editor columns; only labels inherit tree indentation.
        class Row : public Container
        {
          public:

            Row(std::unique_ptr<Control> name, std::unique_ptr<Control> editor, std::unique_ptr<Control> action = {});
            Control& NameControl() const { return *Children()[0]; }
            Control& EditorControl() const { return *Children()[1]; }
            Control* ActionControl() const { return Children().size() == 3 ? Children()[2].get() : nullptr; }
            bool Focusable() const override { return true; }

          protected:

            Size OnMeasure(Size) override;
            void OnArrange() override;
            void OnRender(Canvas&) override;
            bool OnInput(const Input&) override;

          private:

            // Roles are fixed at construction; this is not a general grid or mutable child list.
            using Container::Add;
            using Container::Clear;
            using Container::Emplace;
            using Container::Remove;
            struct Columns
            {
                float nameLeft, nameWidth, editorLeft, editorWidth, actionLeft, actionWidth;
            };
            Columns ColumnLayout(float width) const;
        };
        class Branch : public StackPanel
        {
          public:

            explicit Branch(std::string label);
            StackPanel& Content() { return *content_; }
            void SetExpanded(bool value);
            bool Expanded() const { return expanded_; }
            void RevealForSearch(bool searching);
            Button& Header() { return *header_; }
            LWSUI::Event<void(bool)> OnExpanded;

          protected:

            Size OnMeasure(Size available) override
            {
                spacing = Style().spacing;
                content_->spacing = Style().spacing;
                return StackPanel::OnMeasure(available);
            }

            void OnArrange() override;
            bool OnInput(const Input&) override;

          private:

            // Child roles are owned by this composite.
            using Container::Add;
            using Container::Clear;
            using Container::Emplace;
            using Container::Remove;

            Button* header_;
            StackPanel* content_;
            std::string label_;
            bool expanded_ = true, searching_ = false;
            LWSUI::Event<void()>::Connection click_;
        };

      protected:

        bool OnInput(const Input&) override;
    };
}  // namespace LWSUI
