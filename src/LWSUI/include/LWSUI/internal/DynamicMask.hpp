#pragma once
#include <LWSUI/TextInput.hpp>
#include <algorithm>
#include <limits>

namespace LWSUI::internal
{
    struct DynamicMask
    {
        DynamicMaskDefinition definition;
        struct Edit { std::string display; size_t caret; bool explicitProfile = false; };
        static int Digit(char c)
        {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'z') return c - 'a' + 10;
            if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
            return -1;
        }
        static std::expected<DynamicMask, std::string> Create(const DynamicMaskDefinition& definition)
        {
            if (definition.profiles.empty()) return std::unexpected("A dynamic mask needs profiles");
            std::vector<std::string> prefixes;
            for (const auto& p : definition.profiles)
            {
                if (p.base < 2 || p.base > 36 || !p.fieldCount || p.fieldCount > 1024 || !p.minimumDigits ||
                    p.minimumDigits > p.maximumDigits || p.maximumDigits > 64 || p.minimumValue > p.maximumValue ||
                    p.separator < 33 || p.separator > 126 || (Digit(p.separator) >= 0 && unsigned(Digit(p.separator)) < p.base) ||
                    std::any_of(p.prefix.begin(), p.prefix.end(), [](unsigned char c) { return c < 33 || c > 126; }) ||
                    p.prefix.find(p.separator) != p.prefix.npos || (p.inferFromDigits && p.prefix.empty()) ||
                    std::find(prefixes.begin(), prefixes.end(), p.prefix) != prefixes.end())
                    return std::unexpected("Invalid dynamic mask profile");
                uint64_t capacity = 0;
                for (unsigned i = 0; i < p.maximumDigits; ++i)
                {
                    if (capacity > (std::numeric_limits<uint64_t>::max() - (p.base - 1)) / p.base)
                    { capacity = std::numeric_limits<uint64_t>::max(); break; }
                    capacity = capacity * p.base + p.base - 1;
                }
                if (p.maximumValue > capacity) return std::unexpected("Mask bounds exceed field width");
                prefixes.push_back(p.prefix);
            }
            return DynamicMask{definition};
        }
        const DynamicMaskProfile* Profile(std::string_view display) const
        {
            const DynamicMaskProfile* found = nullptr;
            for (const auto& p : definition.profiles)
                if (display.starts_with(p.prefix) && (!found || p.prefix.size() > found->prefix.size())) found = &p;
            return found;
        }
        bool Provisional(std::string_view text) const
        {
            if (text.empty()) return true;
            for (const auto& p : definition.profiles)
                if (text.size() < p.prefix.size() && std::string_view(p.prefix).starts_with(text)) return true;
            return false;
        }
        static bool Valid(std::string_view digits, const DynamicMaskProfile& p, bool complete = false)
        {
            if (digits.size() > p.maximumDigits) return false;
            if (digits.empty()) return !complete;
            uint64_t value = 0;
            for (char c : digits)
            {
                const int digit = Digit(c);
                if (digit < 0 || unsigned(digit) >= p.base ||
                    value > (std::numeric_limits<uint64_t>::max() - unsigned(digit)) / p.base) return false;
                value = value * p.base + unsigned(digit);
            }
            return value <= p.maximumValue && (!complete || (digits.size() >= p.minimumDigits && value >= p.minimumValue));
        }
        std::expected<Edit, std::string> Format(std::string_view input) const
        {
            if (Provisional(input)) return Edit{std::string(input), input.size()};
            auto* p = Profile(input);
            if (!p) return std::unexpected("Unknown mask prefix");
            std::string display = p->prefix, field;
            unsigned index = 0;
            bool generated = false;
            for (char c : input.substr(p->prefix.size()))
            {
                if (c == p->separator)
                {
                    if (generated) { generated = false; continue; }
                    if (!Valid(field, *p, true) || index + 1 >= p->fieldCount)
                        return std::unexpected("Complete the current field first");
                    display += c;
                    ++index;
                    field.clear();
                    continue;
                }
                generated = false;
                if (p->uppercaseDigits && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
                field += c;
                if (!Valid(field, *p)) return std::unexpected("Input exceeds mask field bounds");
                display += c;
                if (field.size() == p->maximumDigits && index + 1 < p->fieldCount)
                {
                    display += p->separator;
                    ++index;
                    field.clear();
                    generated = true;
                }
            }
            return Edit{display, display.size(), !p->prefix.empty() || input.find(p->separator) != input.npos};
        }
        bool Complete(std::string_view display) const
        {
            if (definition.isComplete) return definition.isComplete(display);
            const auto* p = Profile(display);
            if (!p) return false;
            display.remove_prefix(p->prefix.size());
            for (unsigned i = 0; i < p->fieldCount; ++i)
            {
                const auto end = display.find(p->separator);
                if ((i + 1 < p->fieldCount) != (end != display.npos)) return false;
                if (!Valid(display.substr(0, end), *p, true)) return false;
                if (end != display.npos) display.remove_prefix(end + 1);
            }
            return true;
        }
        std::expected<Edit, std::string> Replace(std::string_view display, size_t first, size_t last,
                                                std::string input, TextEditOrigin origin, bool explicitProfile = false) const
        {
            const auto* current = Profile(display);
            const bool replacingAll = first == 0 && last == display.size();
            if (origin == TextEditOrigin::Paste &&
                (definition.pasteReplacesValue || (definition.normalizePaste &&
                    (replacingAll || (current && display == current->prefix)))))
            {
                const auto prefix = replacingAll || !current ? std::string_view{} : std::string_view(current->prefix);
                auto normalized = definition.normalizePaste ? definition.normalizePaste(input, prefix)
                    : std::expected<std::string, std::string>{input};
                if (!normalized) return std::unexpected(normalized.error());
                return Format(*normalized);
            }
            if (origin == TextEditOrigin::Typing)
            {
                const auto* fallback = Profile("");
                const bool undecided = fallback && (replacingAll || (current == fallback && !explicitProfile));
                if (undecided)
                {
                    auto rawOffset = [&](size_t end)
                    {
                        size_t count = 0;
                        for (size_t i = 0; i < end; ++i) if (display[i] != fallback->separator) ++count;
                        return count;
                    };
                    const size_t rawFirst = replacingAll ? 0 : rawOffset(first);
                    const size_t rawLast = replacingAll ? display.size() : rawOffset(last);
                    auto proposed = std::string(display);
                    if (!replacingAll) std::erase(proposed, fallback->separator);
                    proposed.replace(rawFirst, rawLast - rawFirst, input);
                    bool rangeRejected = false;
                    for (size_t offset = 0; offset < proposed.size(); offset += fallback->maximumDigits)
                        rangeRejected |= !Valid(std::string_view(proposed).substr(offset, fallback->maximumDigits), *fallback);
                    for (const auto& candidate : definition.profiles)
                    {
                        if (!candidate.inferFromDigits) continue;
                        bool distinguishing = false;
                        const bool digits = std::all_of(proposed.begin(), proposed.end(), [&](char c)
                        {
                            const auto digit = Digit(c);
                            distinguishing |= digit >= 0 && unsigned(digit) >= fallback->base;
                            return digit >= 0 && unsigned(digit) < candidate.base;
                        });
                        if (!digits || (!distinguishing && !rangeRejected)) continue;
                        auto result = Format(candidate.prefix + proposed);
                        if (!result) return result;
                        const auto inputCaret = rawFirst + input.size();
                        result->caret = std::min(result->display.size(), candidate.prefix.size() + inputCaret +
                            inputCaret / candidate.maximumDigits);
                        return result;
                    }
                }
            }
            if (replacingAll) return Format(input);
            if (Provisional(display) && first == display.size() && last == first)
                return Format(std::string(display) + input);
            const auto* p = Profile(display);
            if (p && display == p->prefix && input.empty())
            {
                auto remaining = std::string(display);
                remaining.erase(first, last - first);
                return Format(remaining);
            }
            if (!p || first < p->prefix.size()) return std::unexpected("Replace the whole value to change its prefix");
            if (!explicitProfile && p->prefix.empty() && input == std::string(1, p->separator) &&
                first == last && first && display[first - 1] == p->separator)
                return Edit{std::string(display), first, true};
            std::vector<std::string> fields(1);
            std::vector<size_t> offsets{p->prefix.size()};
            for (size_t i = p->prefix.size(); i < display.size(); ++i)
            {
                if (display[i] == p->separator) { fields.emplace_back(); offsets.push_back(i + 1); }
                else fields.back() += display[i];
            }
            size_t active = 0;
            for (size_t i = 0; i < fields.size(); ++i) if (offsets[i] <= first) active = i;
            size_t at = std::min(first - offsets[active], fields[active].size());
            for (size_t i = 0; i < fields.size(); ++i)
            {
                const auto begin = offsets[i], end = begin + fields[i].size();
                if (first < end && last > begin)
                    fields[i].erase(std::max(first, begin) - begin, std::min(last, end) - std::max(first, begin));
            }
            if (input.empty() && std::all_of(fields.begin(), fields.end(), [](const auto& field) { return field.empty(); }))
                return Edit{p->prefix, p->prefix.size()};
            bool generated = false;
            for (char c : input)
            {
                if (c == p->separator)
                {
                    if (generated) { generated = false; continue; }
                    if (!Valid(fields[active], *p, true) || active + 1 >= p->fieldCount)
                        return std::unexpected("Complete the current field first");
                    ++active;
                    if (fields.size() <= active) fields.emplace_back();
                    at = 0;
                    continue;
                }
                generated = false;
                if (p->uppercaseDigits && c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
                fields[active].insert(at, 1, c);
                if (!Valid(fields[active], *p)) return std::unexpected("Input exceeds mask field bounds");
                ++at;
                if (fields[active].size() == p->maximumDigits && active + 1 < p->fieldCount)
                {
                    ++active;
                    if (fields.size() <= active) fields.emplace_back();
                    at = 0;
                    generated = true;
                }
            }
            std::string result = p->prefix;
            size_t caret = result.size();
            for (size_t i = 0; i < fields.size(); ++i)
            {
                if (i) result += p->separator;
                if (i == active) caret = result.size() + at;
                result += fields[i];
            }
            return Edit{result, caret, explicitProfile || !p->prefix.empty() || input.find(p->separator) != input.npos};
        }
    };
}
