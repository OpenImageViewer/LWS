#pragma once
#include <LLUtils/BitFlags.h>
#include <cstdint>
#include <stdexcept>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace LWSUI
{
    enum class HexadecimalForm : uint8_t
    {
        None = 0 << 0,
        Bare = 1 << 0,
        HashPrefix = 1 << 1,
        ZeroXPrefix = 1 << 2
    };
    LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(HexadecimalForm)
    using HexadecimalFormFlags = LLUtils::BitFlags<HexadecimalForm>;
    namespace internal
    {
        inline std::expected<void, std::string> CheckHexadecimal(std::string_view value, HexadecimalFormFlags forms,
                                                                 bool complete,
                                                                 std::optional<unsigned> spacing = std::nullopt,
                                                                 unsigned groupSize = 2)
        {
            if (value.empty())
            {
                if (complete)
                    return std::unexpected("Enter hexadecimal digits");
                return {};
            }
            HexadecimalForm form = HexadecimalForm::Bare;
            if (value.front() == '#')
            {
                form = HexadecimalForm::HashPrefix;
                value.remove_prefix(1);
            }
            else if (value.starts_with("0x") || value.starts_with("0X"))
            {
                form = HexadecimalForm::ZeroXPrefix;
                value.remove_prefix(2);
            }
            else if (!complete && value == "0" && forms.test(HexadecimalForm::ZeroXPrefix))
                return {};
            if (!forms.test(form))
                return std::unexpected("This hexadecimal form is not enabled");
            if (!groupSize) return std::unexpected("Group size must be positive");
            const bool grouped = spacing.value_or(0) != 0;
            if (value.find_first_not_of(grouped ? "0123456789abcdefABCDEF " : "0123456789abcdefABCDEF") != value.npos)
                return std::unexpected("Use hexadecimal digits");
            if (complete && grouped && value.find(' ') != value.npos)
            {
                for (size_t begin = 0; begin < value.size();)
                {
                    auto end = value.find(' ', begin);
                    if (end == value.npos) end = value.size();
                    if (end - begin != groupSize) return std::unexpected("Use complete hexadecimal groups");
                    if (end == value.size()) break;
                    auto next = value.find_first_not_of(' ', end);
                    if (next == value.npos || next - end != *spacing)
                        return std::unexpected("Use the configured spacing between groups");
                    begin = next;
                }
            }
            if (complete && value.empty())
                return std::unexpected("Enter digits after the hexadecimal prefix");
            return {};
        }
    }  // namespace internal
    enum class TextEditOrigin
    {
        Typing,
        Paste,
        Cut,
        Delete
    };
    // An empty result accepts the proposed whole value. An error rejects the entire edit.
    using InputFilter = std::function<std::expected<void, std::string>(std::string_view, TextEditOrigin)>;
    struct DynamicMaskProfile
    {
        std::string prefix;
        unsigned base = 10, fieldCount = 1, minimumDigits = 1, maximumDigits = 3;
        uint64_t minimumValue = 0, maximumValue = 255;
        char separator = ',';
        bool inferFromDigits = false;
        bool uppercaseDigits = false;
    };
    struct DynamicMaskDefinition
    {
        std::vector<DynamicMaskProfile> profiles;
        bool pasteReplacesValue = false;
        std::function<bool(std::string_view)> isComplete;
        // Normalize whole-value paste with an optional existing prefix.
        std::function<std::expected<std::string, std::string>(std::string_view, std::string_view)> normalizePaste;
    };
    namespace InputFilters
    {
        inline InputFilter Number(bool integer = false, bool signedValue = true, bool exponent = true)
        {
            return [=](std::string_view value, TextEditOrigin origin) -> std::expected<void, std::string>
            {
                if (origin == TextEditOrigin::Delete || origin == TextEditOrigin::Cut)
                    return {};
                size_t i = 0;
                if (signedValue && i < value.size() && value[i] == '-')
                    ++i;
                bool dot = false, exp = false, digit = false;
                for (; i < value.size(); ++i)
                {
                    const char c = value[i];
                    if (c >= '0' && c <= '9')
                    {
                        digit = true;
                        continue;
                    }
                    if (!integer && c == '.' && !dot && !exp)
                    {
                        dot = true;
                        continue;
                    }
                    if (!integer && exponent && (c == 'e' || c == 'E') && !exp && digit)
                    {
                        exp = true;
                        if (i + 1 < value.size() && (value[i + 1] == '+' || value[i + 1] == '-'))
                            ++i;
                        continue;
                    }
                    return std::unexpected(integer ? "Use integer digits" : "Use a decimal number");
                }
                return {};
            };
        }
        inline InputFilter Hexadecimal(HexadecimalFormFlags forms, std::optional<unsigned> spacing = std::nullopt,
                                      unsigned groupSize = 2)
        {
            const auto bits = static_cast<unsigned>(forms.get());
            if (!bits || (bits & ~7U))
                throw std::invalid_argument("Choose at least one valid hexadecimal form");
            if (!groupSize) throw std::invalid_argument("Group size must be positive");
            return [forms, spacing, groupSize](std::string_view value, TextEditOrigin)
            { return internal::CheckHexadecimal(value, forms, false, spacing, groupSize); };
        }
        inline InputFilter Hexadecimal(bool color = false, std::optional<unsigned> spacing = std::nullopt,
                                      unsigned groupSize = 2)
        {
            auto syntax = Hexadecimal(
                HexadecimalFormFlags{color ? HexadecimalForm::HashPrefix : HexadecimalForm::Bare}, spacing, groupSize);
            if (!color)
                return syntax;
            return [syntax = std::move(syntax)](std::string_view value,
                                                TextEditOrigin origin) -> std::expected<void, std::string>
            {
                size_t digits = 0;
                for (char c : value) if (c != '#' && c != ' ') ++digits;
                if (digits > 8) return std::unexpected("Use #RRGGBB or #RRGGBBAA");
                return syntax(value, origin);
            };
        }
    }  // namespace InputFilters
}  // namespace LWSUI
