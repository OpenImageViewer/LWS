#pragma once
#include <LWSUI/Control.hpp>
namespace LWSUI::internal
{
    // Inclusive membership only. Callback dispatch still owns its lifetime-safe traversal.
    inline bool IsWithin(const Control* control, const Control* ancestor)
    {
        for (; control; control = control->Parent())
            if (control == ancestor)
                return true;
        return false;
    }
}  // namespace LWSUI::internal
