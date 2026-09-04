#pragma once

#include <LWS/Event.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace LWS::internal
{
    // Wayland describes one logical scroll event through multiple callbacks. The axis callback supplies continuous
    // motion while axis_discrete or axis_value120 may supply a detent-based representation in the same pointer frame.
    // Keep only the current frame so the most precise representation can win without dispatching the coupled axis
    // callback as a duplicate. takeDelta clears all state at the frame boundary.
    class WheelDeltaFrame
    {
      public:

        [[nodiscard]] static constexpr bool usesPointerFrame(uint32_t pointerVersion)
        {
            return pointerVersion >= PointerFrameVersion;
        }

        void addWaylandAxis(double value)
        {
            fContinuousDelta = fContinuousDelta.value_or(0.0) - value * AxisUnitsToDelta;
        }

        void addWaylandDiscrete(int32_t steps) { fDiscreteSteps = fDiscreteSteps.value_or(0) - steps; }

        void addWaylandValue120(int32_t delta) { fValue120 = fValue120.value_or(0) - delta; }

        [[nodiscard]] std::optional<int32_t> takeDelta()
        {
            std::optional<int32_t> delta;
            if (fValue120)
                delta = clamp(*fValue120);
            else if (fDiscreteSteps)
                delta = clamp(*fDiscreteSteps * EventMouseWheel::DeltaPerStep);
            else if (fContinuousDelta)
                delta = clamp(std::llround(*fContinuousDelta));

            clear();
            if (delta == 0)
                delta.reset();
            return delta;
        }

        void clear()
        {
            fContinuousDelta.reset();
            fDiscreteSteps.reset();
            fValue120.reset();
        }

      private:

        static constexpr double AxisUnitsToDelta = 12.0;
        static constexpr uint32_t PointerFrameVersion = 5;

        static int32_t clamp(int64_t value)
        {
            return static_cast<int32_t>(std::clamp(value, static_cast<int64_t>(std::numeric_limits<int32_t>::min()),
                                                   static_cast<int64_t>(std::numeric_limits<int32_t>::max())));
        }

        std::optional<double> fContinuousDelta;
        std::optional<int64_t> fDiscreteSteps;
        std::optional<int64_t> fValue120;
    };
}  // namespace LWS::internal
