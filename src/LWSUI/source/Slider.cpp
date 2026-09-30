#include <LWSUI/Composites.hpp>
#include <algorithm>
namespace LWSUI
{
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
        if (e.kind == InputKind::Cancel || (e.kind == InputKind::KeyDown && e.key == LWS::KeyCode::Escape))
            return Finish(EditPhase::Cancel);
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
