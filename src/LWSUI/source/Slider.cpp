#include <LWSUI/Composites.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <LWS/Timer.hpp>
#include <LWSUI/UIHost.hpp>
namespace LWSUI
{
    Thumbwheel::Thumbwheel() = default;
    Thumbwheel::~Thumbwheel() = default;
    void Thumbwheel::SetSpeedRange(double minimum, double maximum)
    {
        if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum <= 0 || maximum < minimum)
            throw std::invalid_argument("Invalid thumbwheel speed range");
        if (dragging_)
            Tick();
        minimumSpeed_ = minimum;
        maximumSpeed_ = maximum;
    }
    void Thumbwheel::SetAcceleration(double factor)
    {
        if (!std::isfinite(factor) || factor < .1 || factor > 2)
            throw std::invalid_argument("Thumbwheel acceleration must be between 0.1 and 2");
        if (dragging_)
            Tick();
        accelerationExponent_ = 3 / factor;
    }
    void Thumbwheel::SetDeadZone(double fraction)
    {
        if (!std::isfinite(fraction) || fraction < 0 || fraction > .25)
            throw std::invalid_argument("Thumbwheel dead zone must be between 0 and .25");
        if (dragging_)
            Tick();
        deadZone_ = fraction;
        UpdateInteraction();
    }
    void Thumbwheel::SetDragSensitivity(double multiplier)
    {
        if (!std::isfinite(multiplier) || multiplier < .1 || multiplier > 4)
            throw std::invalid_argument("Thumbwheel drag sensitivity must be between .1 and 4");
        dragSensitivity_ = multiplier;
    }
    void Thumbwheel::SetFineModeMultiplier(double multiplier)
    {
        if (!std::isfinite(multiplier) || multiplier < .01 || multiplier > 1)
            throw std::invalid_argument("Thumbwheel fine multiplier must be between .01 and 1");
        if (dragging_)
            Tick();
        fineModeMultiplier_ = multiplier;
    }
    Size Thumbwheel::OnMeasure(Size available)
    {
        return {available.width, 20};
    }
    float Thumbwheel::Travel() const
    {
        return std::max(0.f, Bounds().width / 2 - 4);
    }
    double Thumbwheel::Rate() const
    {
        const double magnitude = std::abs(position_);
        if (magnitude <= deadZone_)
            return 0;
        const double t = (magnitude - deadZone_) / (1 - deadZone_);
        return std::copysign(minimumSpeed_ + (maximumSpeed_ - minimumSpeed_) * std::pow(t, accelerationExponent_),
                             position_) *
               (fine_ ? fineModeMultiplier_ : 1);
    }
    void Thumbwheel::Advance(double seconds)
    {
        const double rate = Rate();
        if (dragging_ && rate != 0 && seconds > 0)
        {
            // Do not turn a delayed UI callback into a large catch-up jump.
            displacement_ += rate * std::min(seconds, .05);
            OnEdit.Raise(displacement_, EditPhase::Preview);
        }
    }
    void Thumbwheel::Tick()
    {
        if (Host())
        {
            bool available = Host()->Window().IsVisible();
            for (const Control* c = this; c; c = c->Parent())
                available = available && c->Visible() && c->Enabled();
            if (!available)
            {
                Finish(EditPhase::Cancel);
                return;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        const auto seconds = std::chrono::duration<double>(now - lastTick_).count();
        lastTick_ = now;
        Advance(seconds);
    }
    void Thumbwheel::UpdateInteraction()
    {
        // A quick click must adjust once even before the first timer tick. Delayed
        // activation (dragging out of the dead zone) shares this one step per gesture.
        if (dragging_ && !activated_ && std::abs(position_) > deadZone_)
        {
            activated_ = true;
            displacement_ += std::copysign(1., position_);
            OnEdit.Raise(displacement_, EditPhase::Preview);
        }
        // Edit callbacks may cancel, hide, or disable the control.
        if (Host() && dragging_ && Rate() != 0)
        {
            if (!timer_)
            {
                timer_ = std::make_unique<LWS::Timer>(Host()->Window().GetPlatformContext());
                if (timer_->SetTargetWindow(&Host()->Window()) != LWS::Result::Success)
                    throw std::runtime_error("Cannot attach thumbwheel timer");
                timer_->SetCallback([this] { Tick(); });
            }
            timer_->SetInterval(16);
        }
        else if (timer_)
            timer_->SetInterval(0);
    }
    void Thumbwheel::OnDetach()
    {
        timer_.reset();
    }
    bool Thumbwheel::Finish(EditPhase phase)
    {
        if (dragging_)
        {
            if (phase == EditPhase::Commit)
                Tick();
            // Tick may cancel if the window or its control ancestry is unavailable.
            if (!dragging_)
                return true;
            dragging_ = false;
            position_ = 0;
            UpdateInteraction();
            ReleaseCapture();
            OnEdit.Raise(phase == EditPhase::Cancel ? 0 : displacement_, phase);
            Invalidate();
        }
        return true;
    }
    bool Thumbwheel::OnInput(const Input& e)
    {
        if (e.kind == InputKind::Down && e.button == LWS::MouseButton::Left)
        {
            if (dragging_)
                return true;
            dragging_ = true;
            fine_ = e.shift;
            lastX_ = e.x;
            displacement_ = 0;
            activated_ = false;
            const auto travel = Travel();
            position_ = travel > 0 ? std::clamp(double(e.x - Bounds().x - Bounds().width / 2) / travel, -1., 1.) : 0;
            lastTick_ = std::chrono::steady_clock::now();
            Capture();
            // Let the consumer snapshot its value before the click step is emitted.
            OnEdit.Raise(0, EditPhase::Preview);
            UpdateInteraction();
            Invalidate();
            return true;
        }
        if (e.kind == InputKind::Move && dragging_)
        {
            Tick();
            if (!dragging_)
                return true;
            const auto travel = Travel();
            position_ = travel > 0 ? std::clamp(position_ + (e.x - lastX_) * dragSensitivity_ / travel, -1., 1.) : 0;
            lastX_ = e.x;
            fine_ = e.shift;
            UpdateInteraction();
            Invalidate();
            return true;
        }
        if (e.kind == InputKind::Up && e.button == LWS::MouseButton::Left && dragging_)
            return Finish(EditPhase::Commit);
        if (e.kind == InputKind::Cancel || (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::Escape && dragging_))
            return Finish(EditPhase::Cancel);
        if (e.kind == InputKind::Wheel ||
            (e.kind == InputKind::KeyDown && (e.key == LWS::KeyCode::Left || e.key == LWS::KeyCode::Right)))
        {
            Finish(EditPhase::Commit);
            OnEdit.Raise(e.kind == InputKind::Wheel    ? e.wheel
                         : e.key == LWS::KeyCode::Left ? -1.
                                                       : 1.,
                         EditPhase::Commit);
            return true;
        }
        if (dragging_ && (e.kind == InputKind::KeyDown || e.kind == InputKind::KeyUp))
        {
            Tick();
            fine_ = e.shift;
            UpdateInteraction();
        }
        return false;
    }
    void Thumbwheel::OnRender(Canvas& canvas)
    {
        const auto b = Bounds();
        const float inset = 2;
        const float width = std::max(0.f, b.width - 2 * inset);
        const float height = std::max(0.f, b.height - 2 * inset);
        canvas.FillRoundedRect(b.x, b.y, b.width, b.height, 2, Style().line);
        // A fixed number of shaded bands gives a cylindrical face at any width.
        // Paired light/dark groove edges and the bevel make the wheel look recessed.
        constexpr int bands = 48;
        const bool light = Style().surface.R() > 128;
        for (int i = 0; i < bands; ++i)
        {
            const double t = (i + .5) / bands;
            const double curve = std::sqrt(std::max(0., 1 - (2 * t - 1) * (2 * t - 1)));
            const int shade = int((light ? 45 : 25) + (Enabled() ? (light ? 160 : 110) : 65) * curve);
            const float left = b.x + inset + width * i / bands;
            const float right = b.x + inset + width * (i + 1) / bands;
            canvas.Fill(left, b.y + inset, right - left, height, LLUtils::Color(shade, shade, shade));
        }
        const float offset = float(std::fmod(position_ * Travel(), 16.0));
        for (float x = b.x + 4 + offset; x < b.x + b.width - 3; x += 16)
        {
            if (x < b.x + 3)
                continue;
            canvas.Line(x, b.y + 3, x, b.y + b.height - 3, 1, LLUtils::Color(90, 90, 90));
            canvas.Line(x + 1, b.y + 3, x + 1, b.y + b.height - 3, 1, LLUtils::Color(175, 175, 175));
        }
        canvas.Line(b.x + 1, b.y + 1, b.x + b.width - 1, b.y + 1, 1, LLUtils::Color(35, 35, 35));
        canvas.Line(b.x + 1, b.y + b.height - 1, b.x + b.width - 1, b.y + b.height - 1, 1,
                    LLUtils::Color(215, 215, 215));
        const float marker = b.x + b.width / 2 + float(position_) * Travel();
        canvas.Line(marker, b.y + 3, marker, b.y + b.height - 3, 3,
                    Enabled() ? LLUtils::Color(202, 35, 35) : Style().muted);
        if (Host() && Host()->IsFocused(*this))
            canvas.StrokeRoundedRect(b.x, b.y, b.width, b.height, CornerRadius(), Style().focusWidth, Style().accent);
    }

    Slider::Slider() = default;
    Slider::~Slider()
    {
        Abort();
    }
    void Slider::SetValue(double value)
    {
        if (valueBinding_)
            valueBinding_->SetNormalized(value);
        else
        {
            value_ = std::clamp(value, 0., 1.);
            Invalidate();
        }
    }
    void Slider::SetValueDisplay(SliderValueMode mode)
    {
        if (!valueBinding_ && mode != SliderValueMode::Hidden)
        {
            auto& value = ConfigureValue<double>({0., 1., keyboardStep}, value_, mode);
            value.normalized_ = true;
            return;
        }
        valueMode_ = mode;
        if (valueBinding_)
            valueBinding_->Display(mode);
        Invalidate(true);
    }
    Control* Slider::ValueField() const
    {
        return valueBinding_ ? valueBinding_->Field() : nullptr;
    }
    float Slider::FieldWidth() const
    {
        return ValueField() ? std::min(std::max(0.f, Bounds().width),
                                       std::max(80.f, 4 * Font().size + (valueMode_ == SliderValueMode::Editable
                                                                             ? Style().rockerWidth
                                                                             : 2 * Style().padding)))
                            : 0;
    }
    Rect Slider::TrackBounds() const
    {
        auto b = Bounds();
        const float field = FieldWidth();
        b.width = std::max(0.f, b.width - field - (field > 0 ? Style().padding : 0));
        return b;
    }
    Size Slider::OnMeasure(Size available)
    {
        auto size = Control::OnMeasure(available);
        if (auto* field = ValueField())
            size.height = std::max(size.height, field->Measure(available).height);
        return size;
    }
    void Slider::OnArrange()
    {
        if (auto* field = ValueField())
        {
            const auto b = Bounds();
            const auto width = FieldWidth();
            field->Measure({width, b.height});
            field->Arrange({b.x + b.width - width, b.y, width, b.height});
        }
    }
    double Slider::Fraction(float x, float) const
    {
        const auto b = TrackBounds();
        if (valueBinding_)
            return std::clamp((x - b.x - grab_) / std::max(1.f, b.width - std::min(Style().sliderThumbWidth, b.width)),
                              0.f, 1.f);
        return std::clamp((x - b.x) / std::max(1.f, b.width), 0.f, 1.f);
    }
    void Slider::Abort()
    {
        dragging_ = false;
        ReleaseCapture();
    }
    void Slider::Publish(EditPhase phase)
    {
        if (valueBinding_)
            valueBinding_->Changed(value_, phase);
        OnEdit.Raise(value_, phase);
    }
    bool Slider::Finish(EditPhase phase)
    {
        if (valueBinding_ && !valueBinding_->Finish(phase))
            return false;
        if (dragging_)
        {
            dragging_ = false;
            ReleaseCapture();
            if (phase == EditPhase::Cancel)
                value_ = start_;
            Publish(phase);
            Invalidate();
        }
        return true;
    }
    bool Slider::OnInput(const Input& e)
    {
        if (e.kind == InputKind::Down)
        {
            if (valueBinding_ && (!TrackBounds().Contains(e.x, e.y) || !valueBinding_->Finish(EditPhase::Commit)))
                return true;
            start_ = value_;
            dragging_ = true;
            if (valueBinding_)
            {
                valueBinding_->Begin();
                const auto b = TrackBounds();
                const float width = std::min(Style().sliderThumbWidth, b.width);
                const float left = b.x + float(value_) * std::max(0.f, b.width - width);
                grab_ = e.x >= left && e.x <= left + width ? e.x - left : width / 2;
            }
            Capture();
        }
        if ((e.kind == InputKind::Down || e.kind == InputKind::Move) && dragging_)
        {
            value_ = Fraction(e.x, e.y);
            Invalidate();
            Publish(EditPhase::Preview);
            return true;
        }
        if (e.kind == InputKind::Up)
            return Finish(EditPhase::Commit);
        if (e.kind == InputKind::Cancel)
            return Finish(EditPhase::Cancel);
        if (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::Escape)
        {
            if (!dragging_ && (!valueBinding_ || !valueBinding_->HasActiveEdit()))
                return false;
            return Finish(EditPhase::Cancel);
        }
        if (e.kind == InputKind::KeyDown && (e.key == LWS::KeyCode::Left || e.key == LWS::KeyCode::Right))
        {
            const int direction = e.key == LWS::KeyCode::Left ? -1 : 1, count = e.shift ? 10 : 1;
            if (valueBinding_)
            {
                if (!Finish(EditPhase::Commit))
                    return true;
                valueBinding_->Step(direction, count);
            }
            else
            {
                SetValue(value_ + direction * keyboardStep * count);
                OnEdit.Raise(value_, EditPhase::Commit);
            }
            return true;
        }
        return false;
    }
    void Slider::OnRender(Canvas& c)
    {
        const auto b = TrackBounds();
        if (filled)
        {
            c.Fill(b.x, b.y, b.width, b.height, Style().surface);
            c.Fill(b.x, b.y, float(value_) * b.width, b.height, Style().accent);
        }
        else
        {
            c.Fill(b.x, b.y + b.height / 2 - Style().sliderThickness / 2, b.width, Style().sliderThickness,
                   Style().surface);
            c.Fill(b.x + float(value_) * std::max(0.f, b.width - Style().sliderThumbWidth), b.y + Style().labelPadding,
                   Style().sliderThumbWidth, b.height - 2 * Style().labelPadding, Style().accent);
        }
        if (auto* field = ValueField())
            field->Render(c);
    }
}  // namespace LWSUI
