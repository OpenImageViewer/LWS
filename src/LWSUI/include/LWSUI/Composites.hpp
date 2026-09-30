#pragma once
#include <LWSUI/NumericSpec.hpp>
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWS/Timer.hpp>
#include <LWSUI/ColorMath.hpp>
#include <charconv>
#include <chrono>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    struct Choice
    {
        std::string label, value;
    };
    class RadioGroup : public StackPanel
    {
      public:

        explicit RadioGroup(std::vector<Choice> choices);
        void SetValue(std::string value);
        const std::string& Value() const { return value_; }
        LWSUI::Event<void(const std::string&)> OnChange;

      protected:

        Size OnMeasure(Size available) override
        {
            spacing = Style().spacing;
            return StackPanel::OnMeasure(available);
        }

        bool OnInput(const Input&) override;

      private:

        // Child roles are owned by this composite.
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;

        std::vector<Choice> choices_;
        std::string value_;
        std::vector<RadioButton*> buttons_;
        std::vector<LWSUI::Event<void(bool)>::Connection> connections_;
    };
    class ComboBox : public Button
    {
      public:

        explicit ComboBox(std::vector<Choice> choices);
        void SetValue(std::string value);
        const std::string& Value() const { return value_; }
        LWSUI::Event<void(const std::string&)> OnChange;

      protected:

        void Activate() override;
        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;

      private:

        std::vector<Choice> choices_;
        std::string value_;
        std::vector<LWSUI::Event<void()>::Connection> choicesConnections_;
    };
    template <class T>
    class NumericEdit : public Container
    {
      public:

        explicit NumericEdit(NumericSpec<T> spec = {}) : spec_(spec)
        {
            if (!std::isfinite(double(spec_.step)) || spec_.step <= 0 ||
                (spec_.minimum && !std::isfinite(double(*spec_.minimum))) ||
                (spec_.maximum && !std::isfinite(double(*spec_.maximum))) ||
                (spec_.minimum && spec_.maximum && *spec_.minimum > *spec_.maximum))
                throw std::invalid_argument("Invalid numeric bounds or step");
            text_ = &Emplace<TextBox>();
            down_ = &Emplace<Button>("▼");
            down_->fontRole = FontRole::Glyph;
            down_->SetHighlightPattern("");
            up_ = &Emplace<Button>("▲");
            up_->fontRole = FontRole::Glyph;
            up_->SetHighlightPattern("");
            textConnection_ = text_->OnEdit.Connect(
                [this](const std::string& text, EditPhase phase)
                {
                    T value{};
                    auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
                    if (error != std::errc{} || end != text.data() + text.size() || !Valid(value))
                    {
                        text_->SetValidation("Enter a number within the allowed bounds");
                        return;
                    }
                    value_ = value;
                    OnEdit.Raise(value_, phase);
                });
            downConnection_ = down_->OnPress.Connect(
                [this](bool pressed)
                {
                    if (pressed)
                    {
                        pointerRepeat_ = true;
                        Start(-1);
                    }
                });
            upConnection_ = up_->OnPress.Connect(
                [this](bool pressed)
                {
                    if (pressed)
                    {
                        pointerRepeat_ = true;
                        Start(1);
                    }
                });
            downRelease_ = down_->OnRelease.Connect([this](EditPhase phase) { Stop(phase); });
            upRelease_ = up_->OnRelease.Connect([this](EditPhase phase) { Stop(phase); });
            SetValue(std::clamp(T{}, spec_.minimum.value_or(std::numeric_limits<T>::lowest()),
                                spec_.maximum.value_or(std::numeric_limits<T>::max())));
        }
        void SetValue(T value)
        {
            if (!Valid(value))
                throw std::invalid_argument("Numeric value is outside its bounds");
            repeating_ = false;
            if (repeatTimer_)
                repeatTimer_->SetInterval(0);
            value_ = value;
            Refresh();
        }
        T Value() const { return value_; }
        bool Finish(EditPhase phase) override
        {
            Stop(phase);
            return Container::Finish(phase);
        }
        LWSUI::Event<void(T, EditPhase)> OnEdit;

      protected:

        Size OnMeasure(Size available) override
        {
            const auto text = text_->Measure({std::max(0.f, available.width - Style().rockerWidth), available.height});
            // The rocker has two glyph rows; its fixed arrangement does not use a grid.
            return {available.width, std::max(text.height, 2 * Style().glyphFont.size * 1.4f)};
        }

        void OnArrange() override
        {
            const auto bounds = Bounds();
            const float rockerWidth = Style().rockerWidth;
            text_->Arrange({bounds.x, bounds.y, std::max(0.f, bounds.width - rockerWidth), bounds.height});
            up_->Arrange({bounds.x + bounds.width - rockerWidth, bounds.y, rockerWidth, bounds.height / 2});
            down_->Arrange(
                {bounds.x + bounds.width - rockerWidth, bounds.y + bounds.height / 2, rockerWidth, bounds.height / 2});
            up_->SetForeground(value_ < spec_.maximum.value_or(std::numeric_limits<T>::max()) ? Style().accent
                                                                                              : Style().muted);
            down_->SetForeground(value_ > spec_.minimum.value_or(std::numeric_limits<T>::lowest()) ? Style().accent
                                                                                                   : Style().muted);
        }
        bool OnInput(const Input& e) override
        {
            if (e.kind == InputKind::KeyDown && (e.key == LWS::KeyCode::Up || e.key == LWS::KeyCode::Down))
            {
                if (!repeating_)
                {
                    pointerRepeat_ = false;
                    Start(e.key == LWS::KeyCode::Up ? 1 : -1);
                }
                return true;
            }
            if (e.kind == InputKind::KeyUp && (e.key == LWS::KeyCode::Up || e.key == LWS::KeyCode::Down))
            {
                Stop(EditPhase::Commit);
                return true;
            }
            if (e.kind == InputKind::Blur)
            {
                Stop(EditPhase::Commit);
                return false;
            }
            if (e.kind == InputKind::Cancel)
            {
                Stop(EditPhase::Cancel);
                return true;
            }
            return false;
        }
        void OnDetach() override
        {
            repeating_ = false;
            repeatTimer_.reset();
        }

      private:

        // Child roles are owned by this composite.
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;

        bool Valid(T value) const
        {
            return std::isfinite(static_cast<double>(value)) && (!spec_.minimum || value >= *spec_.minimum) &&
                   (!spec_.maximum || value <= *spec_.maximum);
        }
        void Refresh()
        {
            char buffer[80];
            auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value_);
            text_->SetText(ec == std::errc{} ? std::string(buffer, end) : "");
        }
        void Start(int direction)
        {
            if (!text_->Finish(EditPhase::Commit))
                return;
            repeating_ = true;
            direction_ = direction;
            start_ = value_;
            started_ = std::chrono::steady_clock::now();
            Step(1);
            if (Host() && repeating_)
            {
                if (!repeatTimer_)
                {
                    repeatTimer_ = std::make_unique<LWS::Timer>(Host()->Window().GetPlatformContext());
                    if (repeatTimer_->SetTargetWindow(&Host()->Window()) != LWS::Result::Success)
                        throw std::runtime_error("Cannot attach numeric repeat timer");
                    repeatTimer_->SetCallback([this] { Repeat(); });
                }
                repeatTimer_->SetInterval(std::max(1, Style().repeatDelayMs));
            }
        }
        void Repeat()
        {
            if (!repeating_ || !Host() || !Host()->Window().IsVisible())
            {
                Stop(EditPhase::Cancel);
                return;
            }
            // Only an active hold has a timer; detached and idle controls own no work.
            repeatTimer_->SetInterval(std::max(1, Style().repeatIntervalMs));
            if (!pointerRepeat_ || (direction_ > 0 ? up_ : down_)->PointerInside())
            {
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         std::chrono::steady_clock::now() - started_)
                                         .count();
                Step(elapsed >= Style().accelerationMs * 2 ? 100 : elapsed >= Style().accelerationMs ? 10 : 1);
            }
        }
        static int DecimalPlaces(double value)
        {
            char buffer[80];
            auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), value);
            const std::string text = error == std::errc{} ? std::string(buffer, end) : std::string{};
            auto e = text.find_first_of("eE");
            const auto dot = text.find('.');
            int digits = dot == std::string::npos ? 0 : int((e == std::string::npos ? text.size() : e) - dot - 1);
            if (e != std::string::npos)
                digits -= std::stoi(text.substr(e + 1));
            return std::max(0, digits);
        }
        void Step(int multiplier)
        {
            T low = spec_.minimum.value_or(std::numeric_limits<T>::lowest()),
              high = spec_.maximum.value_or(std::numeric_limits<T>::max());
            int stepPrecision = 0;
            if constexpr (!std::is_integral_v<T>)
                stepPrecision = DecimalPlaces(spec_.step);
            // Repeated bounded addition preserves rounding and int64 precision.
            for (int i = 0; i < multiplier; ++i)
            {
                if (direction_ > 0 ? value_ == high : value_ == low)
                    break;
                if constexpr (std::is_integral_v<T>)
                {
                    if (direction_ > 0)
                        value_ = uint64_t(high) - uint64_t(value_) < uint64_t(spec_.step) ? high
                                                                                          : T(value_ + spec_.step);
                    else
                        value_ = uint64_t(value_) - uint64_t(low) < uint64_t(spec_.step) ? low : T(value_ - spec_.step);
                }
                else
                {
                    const int precision = std::max(DecimalPlaces(value_), stepPrecision);
                    double next = value_ + direction_ * spec_.step;
                    const double factor = std::pow(10.0, precision);
                    if (std::isfinite(factor) && std::abs(next) <= std::numeric_limits<double>::max() / factor)
                        next = std::round(next * factor) / factor;
                    value_ = T(std::clamp(next, double(low), double(high)));
                }
            }
            Refresh();
            OnEdit.Raise(value_, EditPhase::Preview);
        }
        void Stop(EditPhase phase)
        {
            if (!repeating_)
                return;
            repeating_ = false;
            if (repeatTimer_)
                repeatTimer_->SetInterval(0);
            if (phase == EditPhase::Cancel)
            {
                value_ = start_;
                Refresh();
            }
            OnEdit.Raise(value_, phase);
        }
        NumericSpec<T> spec_;
        T value_{}, start_{};
        int direction_ = 0;
        bool repeating_ = false, pointerRepeat_ = false;
        TextBox* text_;
        Button* down_;
        Button* up_;
        std::chrono::steady_clock::time_point started_;
        std::unique_ptr<LWS::Timer> repeatTimer_;
        LWSUI::Event<void(const std::string&, EditPhase)>::Connection textConnection_;
        LWSUI::Event<void(bool)>::Connection downConnection_, upConnection_;
        LWSUI::Event<void(EditPhase)>::Connection downRelease_, upRelease_;
    };
    class ColorPicker : public Grid
    {
      public:

        ColorPicker();
        void SetValue(LLUtils::Color value);
        LLUtils::Color Value() const { return value_; }
        LWSUI::Event<void(LLUtils::Color, EditPhase)> OnEdit;

      protected:

        Size OnMeasure(Size available) override
        {
            columns = {-1, Style().swatchWidth};
            spacing = Style().swatchGap;
            return Grid::OnMeasure(available);
        }

      private:

        // Child roles are owned by this composite.
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;

        void Open();
        void Refresh();
        ColorSwatch* swatch_;
        TextBox* text_;
        LLUtils::Color value_{uint32_t{0x000000ff}}, start_;
        LWSUI::Event<void()>::Connection open_;
        LWSUI::Event<void(const std::string&, EditPhase)>::Connection textConnection_;
    };
    class FilePicker : public Grid
    {
      public:

        FilePicker();
        // User edits require an existing file or empty text; stored/programmatic values remain unrestricted.
        void SetValue(std::string value);
        const std::string& Value() const { return text_->Text(); }
        LWSUI::Event<void(const std::string&, EditPhase)> OnEdit;

      protected:

        Size OnMeasure(Size available) override
        {
            columns = {-1, Style().browseWidth};
            return Grid::OnMeasure(available);
        }

        bool OnInput(const Input&) override;

      private:

        // Child roles are owned by this composite.
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;

        void Browse();
        TextBox* text_;
        LWSUI::Event<void()>::Connection browse_;
        LWSUI::Event<void(const std::string&, EditPhase)>::Connection edit_;
    };
}  // namespace LWSUI

#include <LWSUI/SliderValue.hpp>
