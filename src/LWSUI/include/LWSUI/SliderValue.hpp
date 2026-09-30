#pragma once
#include <LWSUI/Composites.hpp>
#include <charconv>
#include <cmath>
namespace LWSUI
{
    // A typed value model attached to the existing normalized slider. The authoritative value remains T.
    template <class T>
    class SliderValue final : public internal::SliderValueBinding
    {
      public:

        T Value() const { return value_; }
        void SetValue(T value)
        {
            if (!std::isfinite(double(value)) || value < *spec_.minimum || value > *spec_.maximum)
                throw std::invalid_argument("Slider value is outside its bounds");
            owner_.Abort();
            struct Restore
            {
                bool& flag;
                bool previous;
                ~Restore() { flag = previous; }
            } restore{muted_, muted_};
            muted_ = true;
            number_->Finish(EditPhase::Cancel);
            value_ = start_ = value;
            Refresh();
        }
        Event<void(T, EditPhase)> OnEdit;

      private:

        friend class Slider;
        SliderValue(Slider& owner, NumericSpec<T> spec, T value) : owner_(owner), spec_(spec)
        {
            if (const auto* error = SliderRangeError(spec))
                throw std::invalid_argument(error);
            auto number = std::make_unique<NumericEdit<T>>(spec);
            number_ = number.get();
            owner_.Add(std::move(number));
            auto label = std::make_unique<Label>();
            label_ = label.get();
            label_->verticalAlignment = Alignment::Center;
            label_->trimming = TextTrimming::Ellipsis;
            owner_.Add(std::move(label));
            edited_ = number_->OnEdit.Connect(
                [this](T value, EditPhase phase)
                {
                    if (muted_)
                        return;
                    value_ = value;
                    owner_.value_ = Fraction();
                    label_->SetText(Text(value));
                    owner_.Invalidate();
                    OnEdit.Raise(value_, phase);
                    owner_.OnEdit.Raise(owner_.value_, phase);
                });
            SetValue(value);
        }
        static std::string Text(T value)
        {
            char bytes[80];
            const auto [end, error] = std::to_chars(bytes, bytes + sizeof(bytes), value);
            return error == std::errc{} ? std::string(bytes, end) : std::string{};
        }
        double Fraction() const
        {
            return std::clamp(
                (double(value_) - double(*spec_.minimum)) / (double(*spec_.maximum) - double(*spec_.minimum)), 0., 1.);
        }
        static int DecimalPlaces(double value)
        {
            char bytes[80];
            const auto [end, error] = std::to_chars(bytes, bytes + sizeof(bytes), value);
            if (error != std::errc{})
                return 0;
            const std::string text(bytes, end);
            const auto exponent = text.find_first_of("eE"), dot = text.find('.');
            int digits = dot == text.npos ? 0 : int((exponent == text.npos ? text.size() : exponent) - dot - 1);
            if (exponent != text.npos)
                digits -= std::stoi(text.substr(exponent + 1));
            return std::max(0, digits);
        }
        double Rounded(double value, double origin) const
        {
            const double factor = std::pow(10., std::max(DecimalPlaces(origin), DecimalPlaces(double(spec_.step))));
            if (std::isfinite(factor) && std::abs(value) <= std::numeric_limits<double>::max() / factor)
                value = std::round(value * factor) / factor;
            return std::clamp(value, double(*spec_.minimum), double(*spec_.maximum));
        }
        T FromFraction(double fraction) const
        {
            if (!std::isfinite(fraction))
                throw std::invalid_argument("Non-finite slider fraction");
            if (normalized_)
                return T(fraction);
            if (fraction <= 0)
                return *spec_.minimum;
            if (fraction >= 1)
                return *spec_.maximum;
            const double span = double(*spec_.maximum) - double(*spec_.minimum);
            const double steps = std::round(fraction * span / double(spec_.step));
            if constexpr (std::is_integral_v<T>)
            {
                const T count = T(steps), range = *spec_.maximum - *spec_.minimum;
                if (count > range / spec_.step)
                    return *spec_.maximum;
                return *spec_.minimum + count * spec_.step;
            }
            else
                return T(Rounded(std::fma(steps, spec_.step, *spec_.minimum), *spec_.minimum));
        }
        void Refresh()
        {
            number_->SetValue(value_);
            label_->SetText(Text(value_));
            owner_.value_ = Fraction();
            owner_.Invalidate();
        }
        void SetNormalized(double value) override { SetValue(FromFraction(std::clamp(value, 0., 1.))); }
        void Begin() override { start_ = value_; }
        void Changed(double fraction, EditPhase phase) override
        {
            value_ = phase == EditPhase::Cancel ? start_ : FromFraction(fraction);
            Refresh();
            OnEdit.Raise(value_, phase);
        }
        bool Finish(EditPhase phase) override { return number_->Finish(phase); }
        void Step(int direction, int count) override
        {
            if (!number_->Finish(EditPhase::Commit))
                return;
            T next = value_;
            for (int i = 0; i < count; ++i)
                if (normalized_)
                    next = T(std::clamp(double(next) + direction * owner_.keyboardStep, 0., 1.));
                else if constexpr (std::is_integral_v<T>)
                {
                    if (direction > 0)
                        next = *spec_.maximum - next < spec_.step ? *spec_.maximum : next + spec_.step;
                    else
                        next = next - *spec_.minimum < spec_.step ? *spec_.minimum : next - spec_.step;
                }
                else
                    next = T(Rounded(next + direction * spec_.step, next));
            value_ = next;
            Refresh();
            OnEdit.Raise(value_, EditPhase::Commit);
            owner_.OnEdit.Raise(owner_.value_, EditPhase::Commit);
        }
        void Display(SliderValueMode mode) override
        {
            number_->SetVisible(mode == SliderValueMode::Editable);
            label_->SetVisible(mode == SliderValueMode::ReadOnly);
        }
        Control* Field() const override
        {
            return number_->Visible() ? static_cast<Control*>(number_) : label_->Visible() ? label_ : nullptr;
        }
        Slider& owner_;
        NumericSpec<T> spec_;
        T value_{}, start_{};
        bool muted_ = false, normalized_ = false;
        NumericEdit<T>* number_;
        Label* label_;
        typename Event<void(T, EditPhase)>::Connection edited_;
    };
    template <class T>
    SliderValue<T>& Slider::ConfigureValue(NumericSpec<T> spec, T value, SliderValueMode mode)
    {
        // Validate before replacing the current configuration. Structural edits use UIHost::Post.
        if (const auto* error = SliderRangeError(spec))
            throw std::invalid_argument(error);
        if (!std::isfinite(double(value)) || value < *spec.minimum || value > *spec.maximum)
            throw std::invalid_argument("Slider value is outside its bounds");
        Abort();
        valueBinding_.reset();
        Clear();
        valueMode_ = mode;
        auto binding = std::unique_ptr<SliderValue<T>>(new SliderValue<T>(*this, spec, value));
        auto& result = *binding;
        valueBinding_ = std::move(binding);
        valueBinding_->Display(mode);
        Invalidate(true);
        return result;
    }
}  // namespace LWSUI
