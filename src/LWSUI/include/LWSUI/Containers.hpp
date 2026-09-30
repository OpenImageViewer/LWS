#pragma once
#include <LWSUI/Primitives.hpp>
namespace LWSUI
{
    class StackPanel : public Container
    {
      public:

        Orientation orientation = Orientation::Vertical;
        float spacing = 6;
        bool sizeToContent = false, clipPartialChildren = false;
        float fallbackItemWidth = 100;

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
    struct Insets
    {
        float left = 0, top = 0, right = 0, bottom = 0;
        Insets() = default;
        explicit Insets(float all) : left(all), top(all), right(all), bottom(all) {}
        Insets(float l, float t, float r, float b) : left(l), top(t), right(r), bottom(b) {}
    };
    // One styled surface. Layout and paint remain reusable even when its content is application-specific.
    class Panel : public Container
    {
      public:

        explicit Panel(std::unique_ptr<Control> content = {});
        void SetContent(std::unique_ptr<Control> content);
        Control* Content() const { return content_; }
        Insets padding, border;
        bool paintBackground = true;
        float minimumHeight = 0;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;
        void OnRender(Canvas&) override;

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        Control* content_ = nullptr;
    };
    enum class Dock
    {
        Top,
        Bottom,
        Left,
        Right,
        Fill
    };
    class DockPanel : public Container
    {
      public:

        Control& AddDocked(std::unique_ptr<Control> child, Dock dock, float extent = 0, float maximumFraction = 1);
        std::unique_ptr<Control> Take(Control& child);
        float gap = 0;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        struct Item
        {
            Control* control;
            Dock dock;
            float extent, maximumFraction;
        };
        std::vector<Item> items_;
    };
    // Content-sized horizontal items, wrapping as necessary. Growing items share a row's spare width.
    class FlowPanel : public Container
    {
      public:

        Control& AddItem(std::unique_ptr<Control> child, bool grow = false, float minimumWidth = 0);
        float gap = 6, rowGap = 6, minimumHeight = 0, minimumItemWidth = 0;
        bool alignEnd = false, firstLeading = false;

      protected:

        Size OnMeasure(Size) override;
        void OnArrange() override;

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        struct Item
        {
            Control* control;
            bool grow;
            float minimumWidth;
            float width = 0;
        };
        struct Row
        {
            std::vector<size_t> items;
            float width = 0, height = 0;
        };
        std::vector<Item> items_;
        std::vector<Row> rows_;
    };
    class SplitPanel : public Container
    {
      public:

        SplitPanel(std::unique_ptr<Control> first, std::unique_ptr<Control> second,
                   Orientation orientation = Orientation::Vertical);
        ~SplitPanel() override;
        Control& First() const { return *first_; }
        Control& Second() const { return *second_; }
        Control& Divider() const;
        void SetOrientation(Orientation orientation);
        Orientation GetOrientation() const { return orientation_; }
        void SetRatio(float ratio);
        float Ratio() const { return ratio_; }
        void SetDefaultTrailingSize(float size, float maximumFraction = 1);
        void SetMinimumSizes(float first, float second);
        void SetDividerSize(float size);
        void Reset();
        void CancelDrag();
        bool Dragging() const { return dragging_; }
        bool Finish(EditPhase phase) override;
        Event<void(float, EditPhase)> OnEdit;

      protected:

        Size OnMeasure(Size available) override { return available; }
        void OnArrange() override;
        bool OnPreviewInput(const Input&) override;
        void OnDetach() override { CancelDrag(); }

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        class Grip;
        void Begin(float position);
        void Move(float delta, EditPhase phase);
        void Complete(EditPhase phase);
        float Extent() const;
        Control *first_, *second_;
        Grip* grip_;
        Orientation orientation_;
        float ratio_ = .5f, defaultRatio_ = .5f, firstMinimum_ = 0, secondMinimum_ = 0, dividerSize_ = 8;
        float trailingSize_ = 0, defaultTrailingSize_ = 0, trailingLimit_ = 1, firstSize_ = 0;
        float start_ = 0, startFirst_ = 0, savedRatio_ = .5f, savedTrailing_ = 0;
        Size arrangedSize_;
        bool trailing_ = false, adjusted_ = false, savedAdjusted_ = false, dragging_ = false;
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
