#pragma once
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <algorithm>
namespace LWSUI::internal
{
    template <class T>
    std::string NumericText(T value)
    {
        char bytes[80];
        const auto [end, error] = std::to_chars(bytes, bytes + sizeof(bytes), value);
        return error == std::errc{} ? std::string(bytes, end) : std::string{};
    }
    template <class T>
    std::optional<T> ParseNumericText(std::string_view text)
    {
        T value{};
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
        if (error != std::errc{} || end != text.data() + text.size())
            return {};
        return value;
    }
    inline int NumericDecimalPlaces(double value)
    {
        const auto text = NumericText(value);
        const auto exponent = text.find_first_of("eE"), dot = text.find('.');
        int digits = dot == text.npos ? 0 : int((exponent == text.npos ? text.size() : exponent) - dot - 1);
        if (exponent != text.npos)
            digits -= std::stoi(text.substr(exponent + 1));
        return std::max(0, digits);
    }
}  // namespace LWSUI::internal
