#pragma once
#include <LWSUI/NumericSpec.hpp>
#include <LWSUI/internal/NumericText.hpp>
namespace LWSUI::internal
{
    inline double RoundNumericValue(double value, double origin, int stepDigits)
    {
        const double factor = std::pow(10., std::max(NumericDecimalPlaces(origin), stepDigits));
        if (std::isfinite(factor) && std::abs(value) <= std::numeric_limits<double>::max() / factor)
            value = std::round(value * factor) / factor;
        return value;
    }
    // Valid value/specification, direction +/-1, and a nonnegative step count.
    template <class T>
    T StepNumericValue(T value, const NumericSpec<T>& spec, int direction, int count)
    {
        const T low = spec.minimum.value_or(std::numeric_limits<T>::lowest()),
                high = spec.maximum.value_or(std::numeric_limits<T>::max());
        int stepDigits = 0;
        if constexpr (!std::is_integral_v<T>)
            stepDigits = NumericDecimalPlaces(spec.step);
        // Repeated addition keeps fractional steps stable and preserves the full integer range.
        for (int i = 0; i < count; ++i)
        {
            if (direction > 0 ? value == high : value == low)
                break;
            if constexpr (std::is_integral_v<T>)
                value = direction > 0
                            ? (uint64_t(high) - uint64_t(value) < uint64_t(spec.step) ? high : T(value + spec.step))
                            : (uint64_t(value) - uint64_t(low) < uint64_t(spec.step) ? low : T(value - spec.step));
            else
                value = T(std::clamp(RoundNumericValue(value + direction * spec.step, value, stepDigits),
                                     double(low), double(high)));
        }
        return value;
    }
}  // namespace LWSUI::internal
