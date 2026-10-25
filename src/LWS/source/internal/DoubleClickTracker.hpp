#pragma once
#include <LWS/WindowTypes.hpp>
#include <LWS/MouseButton.hpp>

namespace LWS::internal
{
    struct DoubleClickTracker
    {
        unsigned Press(MouseButton button, uint32_t time, Point position)
        {
            const bool twice = pending_ && button == button_ && time - time_ <= 500 &&
                               position.DistanceSquared(position_) <= 25;
            pending_ = !twice;
            button_ = button;
            time_ = time;
            position_ = position;
            return twice ? 2U : 1U;
        }
        void Reset() { pending_ = false; }

      private:

        bool pending_ = false;
        MouseButton button_ = MouseButton::Left;
        uint32_t time_ = 0;
        Point position_{};
    };
}  // namespace LWS::internal
