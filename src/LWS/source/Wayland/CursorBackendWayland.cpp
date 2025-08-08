// Wayland cursor backend stub implementation.
#include <LWS/Wayland/CursorBackendWayland.hpp>

#ifdef LWS_PLATFORM_WAYLAND

namespace LWS::Wayland
{
    CursorBackendWayland::~CursorBackendWayland()
    {
        // TODO: wl_cursor_theme_destroy(fWlCursorTheme)
    }

    void CursorBackendWayland::setVisible(bool visible)
    {
        fVisible = visible;
        // TODO: if !visible → wl_pointer.set_cursor(nullptr, 0, 0)
        //       if  visible → restore fWlCursor to wl_pointer
    }

    void CursorBackendWayland::setCursorShape(CursorShape shape)
    {
        fShape = shape;
        // TODO: map LWS::CursorShape to Wayland cursor name string (e.g. "left_ptr", "hand1", etc.)
        //       load via wl_cursor_theme_get_cursor(fWlCursorTheme, name)
        //       apply via wl_pointer.set_cursor with wl_cursor_image buffer
    }

    void CursorBackendWayland::setCustomCursor(const BitmapBuffer& bmp)
    {
        // TODO: create wl_shm_pool from bmp.pixels, create wl_buffer with bmp dimensions
        //       apply via wl_pointer.set_cursor with the wl_surface bound to that buffer
        (void)bmp;
    }
}

#endif // LWS_PLATFORM_WAYLAND
