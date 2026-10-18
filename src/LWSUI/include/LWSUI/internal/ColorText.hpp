#pragma once
#include <LWSUI/TextInput.hpp>
#include <LWSUI/TextStyle.hpp>
#include <array>
#include <LLUtils/Color.h>
#include <charconv>
#include <cstdio>
namespace LWSUI::internal
{
    enum class ColorTextFormat { Hash, ZeroX, UpperZeroX, DecimalRgba };
    inline constexpr const char* ColorInputMessage = "Use #RR-GG-BB-AA, 0xRR-GG-BB-AA, or R,G,B,A (0 to 255)";
    inline ColorTextFormat DetectColorFormat(std::string_view text)
    {
        if (text.starts_with('#')) return ColorTextFormat::Hash;
        if (text.starts_with("0X")) return ColorTextFormat::UpperZeroX;
        if (text.starts_with("0x")) return ColorTextFormat::ZeroX;
        return ColorTextFormat::DecimalRgba;
    }
    inline std::string ColorText(LLUtils::Color color, ColorTextFormat format = ColorTextFormat::Hash)
    {
        char text[32];
        if (format == ColorTextFormat::DecimalRgba)
            std::snprintf(text, sizeof(text), "%u,%u,%u,%u", unsigned(color.R()), unsigned(color.G()),
                          unsigned(color.B()), unsigned(color.A()));
        else
        {
            const auto prefix = format == ColorTextFormat::Hash ? "#" : format == ColorTextFormat::ZeroX ? "0x" : "0X";
            std::snprintf(text, sizeof(text), "%s%02X-%02X-%02X-%02X", prefix, unsigned(color.R()), unsigned(color.G()),
                          unsigned(color.B()), unsigned(color.A()));
        }
        return text;
    }
    inline int DigitHex(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }
    struct ColorFields
    {
        std::array<std::string_view, 4> values{};
        size_t count = 0;
        bool hex = false;
    };
    inline std::optional<ColorFields> ReadColorFields(std::string_view text)
    {
        ColorFields fields;
        fields.hex = DetectColorFormat(text) != ColorTextFormat::DecimalRgba;
        if (fields.hex) text.remove_prefix(text.front() == '#' ? 1 : 2);
        const char separator = fields.hex ? (text.find('-') != text.npos ? '-' : ' ') : ',';
        if (fields.hex && text.find(separator) == text.npos)
        {
            for (size_t offset = 0; offset < text.size(); offset += 2)
            {
                if (fields.count == 4) return {};
                fields.values[fields.count++] = text.substr(offset, 2);
            }
            return fields;
        }
        while (true)
        {
            if (fields.count == 4) return {};
            const auto end = text.find(separator);
            auto value = text.substr(0, end);
            if (!fields.hex)
            {
                const auto begin = value.find_first_not_of(" \t\r\n\f\v");
                value = begin == value.npos ? std::string_view{} :
                    value.substr(begin, value.find_last_not_of(" \t\r\n\f\v") - begin + 1);
            }
            fields.values[fields.count++] = value;
            if (end == text.npos) return fields;
            text.remove_prefix(end + 1);
        }
    }
    inline std::optional<unsigned> ReadColorByte(std::string_view digits, bool hex)
    {
        if (digits.empty() || digits.size() > (hex ? 2u : 3u)) return {};
        unsigned value = 0;
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value, hex ? 16 : 10);
        if (error != std::errc{} || end != digits.data() + digits.size() || value > 255) return {};
        return value;
    }
    struct ColorDraft
    {
        LLUtils::Color value{uint32_t{0x000000ff}};
        bool complete = false;
    };
    inline std::optional<ColorDraft> ReadColorDraft(std::string_view text)
    {
        const auto fields = ReadColorFields(text);
        if (!fields) return {};
        ColorDraft draft;
        draft.complete = fields->count >= 3;
        for (size_t channel = 0; channel < fields->count; ++channel)
        {
            const auto digits = fields->values[channel];
            if (digits.empty())
            {
                if (channel < 3) draft.complete = false;
                continue;
            }
            auto value = ReadColorByte(digits, fields->hex);
            if (!value) return {};
            if (fields->hex && channel < 2 && digits.size() != 2) draft.complete = false;
            if (fields->hex && channel >= 2 && digits.size() == 1) *value <<= 4;
            draft.value.channels[channel] = uint8_t(*value);
        }
        return draft;
    }
    inline std::optional<LLUtils::Color> ParseColor(std::string_view text)
    {
        const auto draft = ReadColorDraft(text);
        return draft && draft->complete ? std::optional{draft->value} : std::nullopt;
    }
    inline std::optional<LLUtils::Color> PreviewColor(std::string_view text)
    {
        const auto draft = ReadColorDraft(text);
        return draft ? std::optional{draft->value} : std::nullopt;
    }
    inline std::optional<unsigned> ParseColorByte(std::string_view text)
    {
        const auto format = DetectColorFormat(text);
        bool hex = format != ColorTextFormat::DecimalRgba;
        if (hex) text.remove_prefix(format == ColorTextFormat::Hash ? 1 : 2);
        else hex = text.find_first_not_of("0123456789") != text.npos;
        return ReadColorByte(text, hex);
    }
    inline std::string ColorByteText(unsigned value, ColorTextFormat format)
    {
        if (format == ColorTextFormat::DecimalRgba) return std::to_string(value);
        const auto prefix = format == ColorTextFormat::Hash ? "#" : format == ColorTextFormat::ZeroX ? "0x" : "0X";
        char text[8];
        std::snprintf(text, sizeof(text), "%s%02X", prefix, value);
        return text;
    }
    inline DynamicMaskDefinition ColorByteInputMask()
    {
        DynamicMaskDefinition mask;
        mask.pasteReplacesValue = true;
        mask.isComplete = [](std::string_view text) { return ParseColorByte(text).has_value(); };
        for (const auto* prefix : {"#", "0x", "0X"})
            mask.profiles.push_back({prefix, 16, 1, 1, 2, 0, 255, '-', false, true});
        mask.profiles.front().inferFromDigits = true;
        mask.profiles.push_back({"", 10, 1, 1, 3, 0, 255, ','});
        mask.normalizePaste = [](std::string_view text, std::string_view) -> std::expected<std::string, std::string>
        {
            auto value = ParseColorByte(text);
            if (!value) return std::unexpected("Enter a byte from 0 to 255 or hexadecimal #FF / 0xFF");
            auto format = DetectColorFormat(text);
            if (format == ColorTextFormat::DecimalRgba && text.find_first_not_of("0123456789") != text.npos)
                format = ColorTextFormat::Hash;
            return ColorByteText(*value, format);
        };
        return mask;
    }
    inline constexpr std::array<uint32_t, 4> ColorChannelColors{0xff5555ff, 0x00ff00ff, 0x3399ffff, 0xffffffff};
    inline std::vector<TextSpan> ColorTextSpans(std::string_view text)
    {
        const auto format = DetectColorFormat(text);
        const bool hex = format != ColorTextFormat::DecimalRgba;
        size_t start = hex ? (format == ColorTextFormat::Hash ? 1 : 2) : 0;
        const char separator = hex ? '-' : ',';
        std::vector<TextSpan> spans;
        for (size_t channel = 0; channel < 4 && start < text.size(); ++channel)
        {
            const auto end = text.find(separator, start);
            const auto stop = end == text.npos ? text.size() : end;
            const auto digits = text.substr(start, stop - start);
            const bool valid = std::all_of(digits.begin(), digits.end(), [hex](char c)
            { return hex ? DigitHex(c) >= 0 : c >= '0' && c <= '9'; });
            if (!digits.empty() && valid)
                spans.push_back({{start, digits.size()}, LLUtils::Color{ColorChannelColors[channel]}, LLUtils::Color{uint32_t{0}}});
            if (end == text.npos) break;
            start = end + 1;
        }
        return spans;
    }
    inline std::expected<std::string, std::string> NormalizeColorPaste(std::string_view text, std::string_view prefix)
    {
        const auto whitespace = [](char c)
        { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v'; };
        std::string compact(text);
        std::erase_if(compact, whitespace);
        if (compact.empty()) return std::unexpected(ColorInputMessage);
        auto format = DetectColorFormat(compact);
        const bool bareDecimal = prefix.empty() && compact.size() <= 3 &&
            compact.find_first_not_of("0123456789") == compact.npos && ReadColorByte(compact, false).has_value();
        if (compact.find(',') != compact.npos || bareDecimal)
        {
            std::string normalized;
            unsigned channel = 0;
            while (true)
            {
                if (channel++ >= 4) return std::unexpected(ColorInputMessage);
                const auto end = text.find(',');
                auto digits = text.substr(0, end);
                while (!digits.empty() && whitespace(digits.front())) digits.remove_prefix(1);
                while (!digits.empty() && whitespace(digits.back())) digits.remove_suffix(1);
                if (digits.empty())
                {
                    if (end == text.npos && channel > 1) return normalized;
                    return std::unexpected(ColorInputMessage);
                }
                unsigned value = 0;
                const auto [last, error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
                if (digits.size() > 3 || error != std::errc{} || last != digits.data() + digits.size() || value > 255)
                    return std::unexpected(ColorInputMessage);
                normalized += std::to_string(value);
                if (end == text.npos) return normalized;
                normalized += ',';
                text.remove_prefix(end + 1);
            }
        }
        if (format == ColorTextFormat::DecimalRgba)
        {
            compact.insert(0, prefix.empty() ? "#" : std::string(prefix));
            format = DetectColorFormat(compact);
        }
        const auto start = format == ColorTextFormat::Hash ? 1u : 2u;
        if (compact.find('-') != compact.npos)
        {
            const auto fields = ReadColorFields(compact);
            if (!fields) return std::unexpected(ColorInputMessage);
            for (size_t channel = 0; channel < fields->count; ++channel)
            {
                const auto digits = fields->values[channel];
                if (digits.empty() && channel + 1 == fields->count) continue;
                if (!ReadColorByte(digits, true)) return std::unexpected(ColorInputMessage);
            }
            if (fields->count == 3 && fields->values[0].size() == 2 && fields->values[1].size() == 2 &&
                fields->values[2].size() == 2) compact += "-FF";
            return compact;
        }
        const auto digits = std::string_view(compact).substr(start);
        if (digits.size() > 8 || !std::all_of(digits.begin(), digits.end(), [](char c) { return DigitHex(c) >= 0; }))
            return std::unexpected(ColorInputMessage);
        if (digits.size() == 6) compact += "FF";
        return compact;
    }
    inline DynamicMaskDefinition ColorInputMask()
    {
        DynamicMaskDefinition mask;
        mask.pasteReplacesValue = true;
        mask.isComplete = [](std::string_view text) { return ParseColor(text).has_value(); };
        for (const auto* prefix : {"#", "0x", "0X"})
            mask.profiles.push_back({prefix, 16, 4, 1, 2, 0, 255, '-', false, true});
        mask.profiles.front().inferFromDigits = true;
        mask.profiles.push_back({"", 10, 4, 1, 3, 0, 255, ','});
        mask.normalizePaste = NormalizeColorPaste;
        return mask;
    }
}
