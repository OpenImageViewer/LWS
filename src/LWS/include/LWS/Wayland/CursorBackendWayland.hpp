#pragma once
#include <LWS/interfaces/backends.hpp>

#include <unordered_set>

namespace LWS
{
    class WindowBackendWayland;

    /// Wayland implementation of ICursorBackend.
    /// Protocol mapping:
    ///   setVisible()       → wl_pointer.set_cursor(null) to hide; restore named cursor to show
    ///   setCursorShape()   → wl_cursor_theme_load + wl_cursor_image + wl_pointer.set_cursor
    ///   setCustomCursor()  → not supported until the API carries hotspot and buffer lifetime information
    class CursorBackendWayland : public internal::ICursorBackend
    {
      public:

        CursorBackendWayland();
        ~CursorBackendWayland() override;

        void setVisible(bool visible) override;
        void setCursorShape(CursorShape shape) override;
        [[nodiscard]] Result setCustomCursor(const BitmapBuffer& bmp, Point hotspot) override;
        BackendId backend() const override { return BackendId::Wayland; }

      private:

        friend class WindowBackendWayland;

        void attach(WindowBackendWayland& owner);
        void detach(WindowBackendWayland& owner);
        void apply();

        std::unordered_set<WindowBackendWayland*> fOwners;
        bool fVisible = true;
        CursorShape fShape = CursorShape::Arrow;
    };
}  // namespace LWS
