#pragma once
#include <Windows.h>
#include <optional>

namespace LWS::internal
{
    // Keep magnetic corrections out of subsequent native move proposals.
    class WindowDragAnchor final
    {
      public:

        void Reset() { anchor_.reset(); }
        bool Active() const { return anchor_.has_value(); }
        void Apply(RECT& rectangle, POINT cursor)
        {
            if (!anchor_ || rectangle.right - rectangle.left != anchor_->rectangle.right - anchor_->rectangle.left ||
                rectangle.bottom - rectangle.top != anchor_->rectangle.bottom - anchor_->rectangle.top)
            {
                // The first move preserves native grab placement. Native size changes rebase for DPI or restoration.
                anchor_ = Anchor{rectangle, cursor};
                return;
            }
            const LONG left = anchor_->rectangle.left + cursor.x - anchor_->cursor.x;
            const LONG top = anchor_->rectangle.top + cursor.y - anchor_->cursor.y;
            OffsetRect(&rectangle, left - rectangle.left, top - rectangle.top);
        }

      private:

        struct Anchor
        {
            RECT rectangle;
            POINT cursor;
        };
        std::optional<Anchor> anchor_;
    };
}  // namespace LWS::internal
