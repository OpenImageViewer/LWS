#pragma once
#include <LWSUI/NumericSpec.hpp>
#include <LWSUI/internal/NumericMath.hpp>
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWS/Timer.hpp>
#include <charconv>
#include <expected>
#include <chrono>
#include <limits>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    template <class T>
    class NumericEdit : public Container
    {
      public:

        explicit NumericEdit(NumericSpec<T> spec = {}) : spec_(spec)
        {
            if (!ValidNumericSpec(spec_))
                throw std::invalid_argument("Invalid numeric bounds or step");
            text_ = &Emplace<TextBox>();
            text_->SetInputFilter(InputFilters::Number(std::is_integral_v<T>, std::is_signed_v<T>));
            down_ = &Emplace<Button>("▼");
            down_->fontRole = FontRole::Glyph;
            down_->SetHighlightPattern("");
            up_ = &Emplace<Button>("▲");
            up_->fontRole = FontRole::Glyph;
            up_->SetHighlightPattern("");
            textConnection_ = text_->OnEdit.Connect(
                [this](const std::string&, EditPhase phase)
                {
                    const auto parsed = Draft();
                    if (!parsed)
                    {
                        text_->SetValidation(parsed.error());
                        return;
                    }
                    value_ = *parsed;
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
            if (!ValidNumericValue(value, spec_))
                throw std::invalid_argument("Numeric value is outside its bounds");
            repeating_ = false;
            if (repeatTimer_)
                repeatTimer_->SetInterval(0);
            value_ = value;
            Refresh();
        }
        T Value() const { return value_; }
        // Inspect the draft without accepting it or publishing edit events.
        std::expected<T, std::string> Draft() const
        {
            const auto parsed = internal::ParseNumericText<T>(text_->Text());
            if (!parsed)
                return std::unexpected(std::is_integral_v<T> ? "Enter a valid integer" : "Enter a valid number");
            if (!ValidNumericValue(*parsed, spec_))
            {
                if (spec_.minimum && spec_.maximum)
                    return std::unexpected("Enter a value from " + internal::NumericText(*spec_.minimum) +
                                           " to " + internal::NumericText(*spec_.maximum));
                if (spec_.minimum) return std::unexpected("Enter a value of at least " + internal::NumericText(*spec_.minimum));
                if (spec_.maximum) return std::unexpected("Enter a value no greater than " + internal::NumericText(*spec_.maximum));
                return std::unexpected("Enter a finite number within the allowed bounds");
            }
            return *parsed;
        }
        void SetValidation(std::string message) { text_->SetValidation(std::move(message)); }
        Control& FocusTarget() const { return *text_; }
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

        void Refresh()
        {
            text_->SetText(internal::NumericText(value_));
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
        void Step(int multiplier)
        {
            value_ = internal::StepNumericValue(value_, spec_, direction_, multiplier);
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
}  // namespace LWSUI
#include <LWSUI/SliderValue.hpp>
