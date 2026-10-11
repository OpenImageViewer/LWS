#pragma once
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace LWSUI::internal
{
    // Display offsets are ordinary TextBox offsets. No second caret coordinate system is needed.
    struct InputMask
    {
        std::string empty;
        std::vector<char> slots;
        static std::expected<InputMask, std::string> Parse(std::string_view pattern)
        {
            InputMask mask;
            bool escaped = false, hasSlot = false;
            for (unsigned char c : pattern)
            {
                if (c < 32 || c > 126 || c == '_')
                    return std::unexpected("Masks require printable ASCII without underscores");
                if (!escaped && c == '\\')
                {
                    escaped = true;
                    continue;
                }
                const char slot = !escaped && (c == '0' || c == 'A' || c == 'X') ? char(c) : 0;
                mask.slots.push_back(slot);
                mask.empty += slot ? '_' : char(c);
                hasSlot |= slot != 0;
                escaped = false;
            }
            if (escaped || (!pattern.empty() && !hasSlot))
                return std::unexpected("A mask needs slots and complete escapes");
            return mask;
        }
        bool Accept(size_t i, char c) const
        {
            const bool digit = c >= '0' && c <= '9';
            const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
            return slots[i] == '0' ? digit : slots[i] == 'A' ? letter : digit || letter;
        }
        size_t Next(size_t i) const
        {
            while (i < slots.size() && !slots[i])
                ++i;
            return i;
        }
        std::string Value(std::string_view display, bool raw = false) const
        {
            std::string result;
            bool any = false;
            for (size_t i = 0; i < display.size(); ++i)
            {
                if (slots[i] && display[i] != '_')
                    any = true;
                if (display[i] != '_' && (!raw || slots[i]))
                    result += display[i];
            }
            return any ? result : std::string{};
        }
        std::expected<size_t, std::string> Replace(std::string& display, size_t first, size_t last,
                                                   std::string_view input) const
        {
            for (size_t i = first; i < last; ++i)
                if (slots[i])
                    display[i] = '_';
            size_t at = first;
            for (char c : input)
            {
                if (at < slots.size() && !slots[at] && c == empty[at])
                {
                    ++at;
                    continue;
                }
                at = Next(at);
                if (at == slots.size() || !Accept(at, c))
                    return std::unexpected("Input does not match the mask");
                display[at++] = c;
            }
            return input.empty() ? first : Next(at);
        }
        std::expected<std::string, std::string> Format(std::string_view value) const
        {
            std::string result = empty;
            auto applied = Replace(result, 0, 0, value);
            if (!applied)
                return std::unexpected(applied.error());
            return result;
        }
    };
}  // namespace LWSUI::internal
