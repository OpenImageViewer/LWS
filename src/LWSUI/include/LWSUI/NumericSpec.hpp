#pragma once
#include <optional>
#include <cmath>
#include <cstdint>
#include <limits>
#include <type_traits>
namespace LWSUI
{
    template <class T>
    struct NumericSpec
    {
        std::optional<T> minimum, maximum;
        T step = []
        {
            if constexpr (std::is_integral_v<T>)
                return T{1};
            else
                return T{.1};
        }();
    };
    template <class T>
    const char* SliderRangeError(const NumericSpec<T>& spec)
    {
        if (!spec.minimum || !spec.maximum || !std::isfinite(double(*spec.minimum)) ||
            !std::isfinite(double(*spec.maximum)) || *spec.minimum >= *spec.maximum ||
            !std::isfinite(double(spec.step)) || spec.step <= 0)
            return "Slider presentation requires finite min < max and a positive step";
        if constexpr (std::is_integral_v<T>)
        {
            constexpr long double exact = 9007199254740991.L;
            const long double span = (long double) *spec.maximum - (long double) *spec.minimum;
            if (*spec.minimum < -exact || *spec.maximum > exact || span > exact ||
                span > static_cast<long double>(std::numeric_limits<T>::max()))
                return "Integer slider bounds and span exceed the exact supported range; use numeric presentation";
        }
        const double span = double(*spec.maximum) - double(*spec.minimum);
        if (!std::isfinite(span) || !std::isfinite(span / double(spec.step)))
            return "Slider range or step count is too large";
        return nullptr;
    }
}  // namespace LWSUI
