#pragma once
#include <LWS/interfaces/backends.hpp>

namespace LWS
{
    /// Wayland implementation of ICursorBackend.
    /// Protocol mapping:
    ///   setVisible()       → wl_pointer.set_cursor(null) to hide; restore named cursor to show
    ///   setCursorShape()   → wl_cursor_theme_load + wl_cursor_image + wl_pointer.set_cursor
    ///   setCustomCursor()  → wl_buffer from shm pool (BitmapBuffer pixels → wl_shm)
    class CursorBackendWayland : public ICursorBackend
    {
    public:
        CursorBackendWayland() = default;
        ~CursorBackendWayland() override;

        void setVisible(bool visible) override;
        void setCursorShape(CursorShape shape) override;
        void setCustomCursor(const BitmapBuffer& bmp) override;
        BackendId backend() const override { return BackendId::Wayland; }

    private:
        bool fVisible = true;
        CursorShape fShape = CursorShape::Arrow;
        void* fWlCursorTheme = nullptr;  // wl_cursor_theme*
        void* fWlCursor = nullptr;       // wl_cursor*
    };
}
