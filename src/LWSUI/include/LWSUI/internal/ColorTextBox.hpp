#pragma once
#include <LWSUI/Primitives.hpp>
#include <LWSUI/internal/ColorText.hpp>

namespace LWSUI::internal
{
    inline constexpr uint32_t ColorInputBackground = 0x161616ff;
    template <class Apply>
    bool ToggleColorNotation(ColorTextFormat current, ColorTextFormat& preferredHex, Apply apply)
    {
        const auto remembered = current == ColorTextFormat::DecimalRgba ? preferredHex : current;
        const auto next = current == ColorTextFormat::DecimalRgba ? remembered : ColorTextFormat::DecimalRgba;
        if (!apply(next)) return false;
        preferredHex = remembered;
        return true;
    }
    inline bool ToggleColorTextBox(TextBox& text, ColorTextFormat& preferredHex)
    {
        const auto color = PreviewColor(text.Text());
        return color && ToggleColorNotation(DetectColorFormat(text.Text()), preferredHex,
            [&](auto next) { return text.EditText(ColorText(*color, next)); });
    }
    inline bool ToggleColorByteTextBox(TextBox& text, size_t channel, ColorTextFormat& preferredHex)
    {
        const auto value = ParseColorByte(text.Text()).value_or(channel == 3 ? 255u : 0u);
        return ToggleColorNotation(DetectColorFormat(text.Text()), preferredHex,
            [&](auto next) { return text.EditText(ColorByteText(value, next)); });
    }
    inline void ConfigureColorTextBox(TextBox& text, std::optional<size_t> channel = std::nullopt,
                                      std::function<bool()> toggle = {})
    {
        text.SetDoubleClickAction(std::move(toggle));
        text.SetBackground(LLUtils::Color{ColorInputBackground});
        text.SetForeground(LLUtils::Color{uint32_t{0xffffffff}});
        text.SetTextSpanProvider([channel](std::string_view value)
        {
            auto spans = ColorTextSpans(value);
            if (channel)
                for (auto& span : spans) span.foreground = LLUtils::Color{ColorChannelColors.at(*channel)};
            return spans;
        });
        text.SetFocusOutline(true);
        text.SetFontWeight(600);
        text.SetMinimumFontSize(16);
    }
}
