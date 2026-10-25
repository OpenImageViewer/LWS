#pragma once
#include <LWSUI/Primitives.hpp>
#include <LWSUI/internal/TextPresentation.hpp>
#include <sstream>
namespace LWSUI::demo
{
    // Fixed-size feedback updates repaint without changing the surrounding layout.
    class SpeedReadout final : public Control
    {
      public:
        void SetSpeed(double speed)
        {
            std::ostringstream text;
            text.precision(3);
            text << speed << "%";
            if (text_ == text.str()) return;
            text_ = text.str();
            Invalidate();
        }
      protected:
        Size OnMeasure(Size available) override
        {
            return {std::min(available.width, std::max(110.f, Font().size * 8.5f + 2 * Style().labelPadding)),
                    std::max(Style().rowHeight, Font().size * 1.4f + 2 * Style().labelPadding)};
        }
        Size OnMeasureContent(Size available) override { return OnMeasure(available); }
        void OnRender(Canvas& canvas) override
        {
            auto b = Bounds();
            b.x += Style().labelPadding;
            b.width = std::max(0.f, b.width - 2 * Style().labelPadding);
            const auto text = internal::AlignedTextBounds(canvas, text_, b, Font(), Alignment::Start, Alignment::Center);
            DrawText(canvas, text_, text.x, text.y, text.width, text.height, Foreground());
        }
      private:
        std::string text_ = "100%";
    };
}
