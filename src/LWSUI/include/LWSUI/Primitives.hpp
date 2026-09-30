#pragma once
#include <LWSUI/Control.hpp>
#include <cstdint>
namespace LWS
{
    class HighPrecisionTimer;
}
namespace LWSUI
{
    class Label : public Control
    {
      public:

        explicit Label(std::string text = {}) : text_(std::move(text)) {}
        void SetText(std::string text)
        {
            if (text_ == text)
                return;
            text_ = std::move(text);
            measurement_.reset();
            Invalidate(true);
        }
        const std::string& Text() const { return text_; }
        bool wrap = false;

      protected:

        void OnRender(Canvas&) override;
        Size OnMeasure(Size) override;
        void OnDetach() override { measurement_.reset(); }

      private:

        struct Measurement
        {
            FontSpec font;
            std::optional<LWS::ContentScale> scale;
            float width;
            bool wrap;
            float height;
        };
        // One native text-height result; text stays owned only by the label.
        std::optional<Measurement> measurement_;
        std::string text_;
    };
    class Button : public Control
    {
      public:

        explicit Button(std::string text = {}) : text_(std::move(text)) {}
        void SetText(std::string text)
        {
            text_ = std::move(text);
            Invalidate();
        }
        const std::string& Text() const { return text_; }
        bool Focusable() const override { return true; }
        bool flat = false;
        LWSUI::Event<void()> OnClick;
        LWSUI::Event<void(bool)> OnPress;
        LWSUI::Event<void(EditPhase)> OnRelease;
        bool PointerInside() const { return pointerInside_; }
        bool Finish(EditPhase phase) override;

      protected:

        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;
        virtual void Activate() { OnClick.Raise(); }
        std::string text_;
        bool pressed_ = false, pointerInside_ = true;
    };
    class CheckBox : public Button
    {
      public:

        explicit CheckBox(std::string text = {}) : Button(std::move(text)) {}
        void SetValue(bool value)
        {
            value_ = value;
            Invalidate();
        }
        bool Value() const { return value_; }
        LWSUI::Event<void(bool)> OnChange;

      protected:

        void OnRender(Canvas&) override;
        void Activate() override;

      private:

        bool value_ = false;
    };
    class RadioButton : public CheckBox
    {
      public:

        using CheckBox::CheckBox;

      protected:

        void OnRender(Canvas&) override;
        Size OnMeasure(Size available) override
        {
            return {available.width, std::max(Style().radioHeight, Font().size * 1.4f + Style().padding)};
        }

        void Activate() override
        {
            if (!Value())
            {
                SetValue(true);
                OnChange.Raise(true);
            }
        }
    };
    enum class TextBoxMode
    {
        SingleLine,
        Multiline
    };
    class ScrollBar;
    class TextBox : public Container
    {
      public:

        explicit TextBox(std::string value = {}, TextBoxMode mode = TextBoxMode::SingleLine);
        ~TextBox() override;
        TextBoxMode Mode() const { return mode_; }
        // Preferred multiline height; allocation remains the parent's responsibility.
        void SetVisibleLines(unsigned lines);
        void SetText(std::string value);
        // Read-only retains selection/copy; enabling it cancels the current draft.
        void SetReadOnly(bool value);
        void SetBorderless(bool value)
        {
            if (borderless_ != value)
            {
                borderless_ = value;
                Invalidate();
            }
        }
        const std::string& Text() const { return text_; }
        void SetPlaceholder(std::string text)
        {
            placeholder_ = std::move(text);
            Invalidate();
        }
        void SetValidation(std::string message)
        {
            validation_ = std::move(message);
            Invalidate();
        }
        const std::string& Validation() const { return validation_; }
        bool Focusable() const override { return true; }
        bool Finish(EditPhase phase) override;
        // Preview may reject a draft by setting validation. Silent SetText resets the edit.
        LWSUI::Event<void(const std::string&, EditPhase)> OnEdit;

      protected:

        Size OnMeasure(Size available) override;
        void OnArrange() override { UpdateGeometry(); }
        void OnDetach() override;
        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        void SelectAll();
        void Copy(bool cut);
        void Paste();
        std::vector<ContextMenuItem> TextMenu(const ContextMenuRequest&);
        void UpdateGeometry(bool reveal = false);
        void SetCaret(TextPosition position, bool extend);
        TextPosition Position(float x, float y);
        size_t CaretLine() const;
        void SetScrollOffset(float offset);
        void StopSelecting();
        void SelectPointer(float x, float y);
        void Replace(std::string text);
        const TextBoxMode mode_;
        unsigned visibleLines_ = 4;
        ScrollBar* scrollbar_ = nullptr;
        Event<void(double, EditPhase)>::Connection scrollConnection_;
        std::unique_ptr<LWS::HighPrecisionTimer> selectionTimer_;
        Rect viewport_;
        Size geometrySize_;
        std::vector<TextLine> lines_;
        TextCaret caretBounds_;
        std::optional<float> preferredX_;
        float scrollY_ = 0, contentHeight_ = 0, geometryGutter_ = 0, pointerX_ = 0, pointerY_ = 0;
        bool caretUpstream_ = false;
        std::string text_, committed_, validation_, placeholder_;
        size_t caret_ = 0, anchor_ = 0;
        uint64_t pasteGeneration_ = 0;
        float scroll_ = 0;
        bool editing_ = false, selecting_ = false, readOnly_ = false, borderless_ = false;
        // Host text metrics are refreshed on text/font changes, independently of paint.
        // Multiline geometry also depends on viewport dimensions and the scrollbar gutter.
        bool geometryDirty_ = true;
        FontSpec geometryFont_;
        float textWidth_ = 0, caretX_ = 0;
        size_t geometryCaret_ = std::string::npos;
    };
    class Slider : public Control
    {
      public:

        void SetValue(double value)
        {
            value_ = std::clamp(value, 0.0, 1.0);
            Invalidate();
        }
        double Value() const { return value_; }
        bool filled = false;
        double keyboardStep = .01;
        bool Focusable() const override { return true; }
        bool Finish(EditPhase phase) override;
        LWSUI::Event<void(double, EditPhase)> OnEdit;

      protected:

        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;
        virtual double Fraction(float x, float y) const;
        bool dragging_ = false;
        double value_ = 0, start_ = 0;
    };
    class ScrollBar : public Slider
    {
      public:

        void SetPage(double page)
        {
            page_ = std::clamp(page, 0.0, 1.0);
            Invalidate();
        }

      protected:

        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;
        double Fraction(float, float) const override;

      private:

        Rect Thumb() const;
        double page_ = 1;
        float grab_ = 0.5f;
    };
    class ColorSwatch : public Button
    {
      public:

        void SetValue(LLUtils::Color value)
        {
            value_ = value;
            Invalidate();
        }
        LLUtils::Color Value() const { return value_; }

      protected:

        void OnRender(Canvas&) override;

      private:

        LLUtils::Color value_{uint32_t{0x000000ff}};
    };
}  // namespace LWSUI
