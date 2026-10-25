#pragma once
#include <LLUtils/Color.h>
#include <algorithm>
#include <compare>
#include <string>
#include <string_view>
#include <vector>
namespace LWSUI
{
    // Logical pixels. Empty family selects the platform UI font.
    struct FontSpec
    {
        float size = 16;
        std::string family;
        int weight = 400;
        bool italic = false;
        auto operator<=>(const FontSpec&) const = default;
    };
    struct TextRange
    {
        size_t offset = 0, length = 0;  // UTF-8 byte offsets in the original string.
        bool operator==(const TextRange&) const = default;
    };
    struct TextPosition
    {
        size_t offset = 0;      // UTF-8 byte boundary.
        bool upstream = false;  // At a soft wrap, use the preceding line's trailing edge.
    };
    struct TextCaret
    {
        float x = 0, y = 0, height = 0;
    };
    struct TextLine
    {
        TextRange range;  // Excludes a hard newline; includes trailing spaces at a soft wrap.
        float y = 0, height = 0;
    };
    struct TextSpan : TextRange
    {
        LLUtils::Color foreground, background;
    };
    inline std::string FoldSearchText(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c += 'a' - 'A';
        return text;
    }
    // Literal, ASCII-case-insensitive matching. Merge overlapping/adjacent ranges
    // for painting while retaining the exact UTF-8 offsets and original spelling.
    inline std::vector<TextRange> FindTextMatches(std::string_view text, std::string_view pattern)
    {
        std::vector<TextRange> result;
        if (pattern.empty())
            return result;
        const auto haystack = FoldSearchText(std::string(text));
        const auto needle = FoldSearchText(std::string(pattern));
        for (size_t at = 0; (at = haystack.find(needle, at)) != std::string::npos; ++at)
        {
            if ((static_cast<unsigned char>(text[at]) & 0xc0) == 0x80)
                continue;
            if (!result.empty() && at <= result.back().offset + result.back().length)
                result.back().length = std::max(result.back().offset + result.back().length, at + needle.size()) -
                                       result.back().offset;
            else
                result.push_back({at, needle.size()});
        }
        return result;
    }
}  // namespace LWSUI
