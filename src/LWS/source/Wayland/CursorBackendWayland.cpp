#include <LWS/source/Wayland/internal/CursorBackendWayland.hpp>

#ifdef LWS_PLATFORM_WAYLAND

    #include "internal/PlatformState.hpp"
    #include "internal/WaylandCursorController.hpp"

    #include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>

    #include <algorithm>
    #include <array>
    #include <charconv>
    #include <cstdlib>
    #include <span>
    #include <string_view>
    #include <utility>

    #include <wayland-client.h>

    #ifdef LWS_HAS_WAYLAND_CURSOR
        #include <wayland-cursor.h>
    #endif

namespace
{
    #ifdef LWS_HAS_WAYLAND_CURSOR
    constexpr int32_t defaultCursorSize = 24;
    constexpr int32_t maxCursorSize = 256;

    int32_t cursorSize()
    {
        const char* text = std::getenv("XCURSOR_SIZE");
        int32_t size = defaultCursorSize;
        if (text != nullptr)
        {
            const std::string_view value{text};
            int32_t configuredSize = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), configuredSize);
            if (error == std::errc{} && end == value.data() + value.size() && configuredSize > 0)
                size = std::min(configuredSize, maxCursorSize);
        }
        return size;
    }

    const char* cursorTheme()
    {
        const char* theme = std::getenv("XCURSOR_THEME");
        return theme != nullptr && *theme != '\0' ? theme : nullptr;
    }

    std::span<const char* const> cursorNames(LWS::CursorShape shape)
    {
        using LWS::CursorShape;
        static constexpr std::array arrow{"left_ptr", "default"};
        static constexpr std::array hand{"pointer", "hand2"};
        static constexpr std::array text{"text", "xterm"};
        static constexpr std::array sizeNS{"ns-resize", "n-resize", "s-resize", "top_side", "bottom_side"};
        static constexpr std::array sizeEW{"ew-resize", "e-resize", "w-resize", "right_side", "left_side"};
        static constexpr std::array sizeNWSE{"nwse-resize", "nw-resize", "se-resize", "top_left_corner",
                                             "bottom_right_corner"};
        static constexpr std::array sizeNESW{"nesw-resize", "ne-resize", "sw-resize", "top_right_corner",
                                             "bottom_left_corner"};
        static constexpr std::array sizeAll{"all-resize", "move"};
        static constexpr std::array no{"not-allowed", "crossed_circle"};
        static constexpr std::array wait{"wait", "watch"};
        static constexpr std::array appStarting{"progress", "left_ptr_watch"};
        switch (shape)
        {
            case CursorShape::Arrow:
                return arrow;
            case CursorShape::Hand:
                return hand;
            case CursorShape::IBeam:
                return text;
            case CursorShape::SizeNS:
                return sizeNS;
            case CursorShape::SizeEW:
                return sizeEW;
            case CursorShape::SizeNWSE:
                return sizeNWSE;
            case CursorShape::SizeNESW:
                return sizeNESW;
            case CursorShape::SizeAll:
                return sizeAll;
            case CursorShape::No:
                return no;
            case CursorShape::Wait:
                return wait;
            case CursorShape::AppStarting:
                return appStarting;
        }
        return arrow;
    }

    wl_cursor* findCursor(wl_cursor_theme* theme, LWS::CursorShape shape)
    {
        if (theme != nullptr)
        {
            for (const char* name : cursorNames(shape))
            {
                if (wl_cursor* cursor = wl_cursor_theme_get_cursor(theme, name);
                    cursor != nullptr && cursor->image_count > 0)
                {
                    return cursor;
                }
            }
        }
        return nullptr;
    }
    #endif
}  // namespace

namespace LWS::internal
{
    class WaylandCursorController::NativeState
    {
      public:

        ~NativeState()
        {
            if (surface != nullptr)
                wl_surface_destroy(surface);
    #ifdef LWS_HAS_WAYLAND_CURSOR
            if (theme != nullptr)
                wl_cursor_theme_destroy(theme);
            if (fallbackTheme != nullptr)
                wl_cursor_theme_destroy(fallbackTheme);
    #endif
        }

        void initialize(wl_compositor* compositor, wl_shm* sharedMemory, int32_t requestedScale)
        {
            if (surface == nullptr)
                surface = wl_compositor_create_surface(compositor);
            if (surface == nullptr)
                return;
    #ifdef LWS_HAS_WAYLAND_CURSOR
            if (!themeLoaded || scale != requestedScale)
            {
                if (theme != nullptr)
                    wl_cursor_theme_destroy(std::exchange(theme, nullptr));
                if (fallbackTheme != nullptr)
                    wl_cursor_theme_destroy(std::exchange(fallbackTheme, nullptr));
                fallbackThemeLoaded = false;
                scale = requestedScale;
                size = cursorSize() * scale;
                theme = wl_cursor_theme_load(cursorTheme(), size, sharedMemory);
                themeLoaded = true;
                if (wl_surface_get_version(surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
                    wl_surface_set_buffer_scale(surface, scale);
            }
    #endif
        }

    #ifdef LWS_HAS_WAYLAND_CURSOR
        wl_cursor* selectCursor(CursorShape shape, wl_shm* sharedMemory)
        {
            wl_cursor* cursor = findCursor(theme, shape);
            if (cursor == nullptr)
            {
                if (!fallbackThemeLoaded)
                {
                    fallbackTheme = wl_cursor_theme_load("Adwaita", size, sharedMemory);
                    fallbackThemeLoaded = true;
                }
                cursor = findCursor(fallbackTheme, shape);
            }
            return cursor;
        }
    #endif

        wl_surface* surface = nullptr;
    #ifdef LWS_HAS_WAYLAND_CURSOR
        wl_cursor_theme* theme = nullptr;
        wl_cursor_theme* fallbackTheme = nullptr;
        int32_t size = defaultCursorSize;
        int32_t scale = 1;
        bool themeLoaded = false;
        bool fallbackThemeLoaded = false;
    #endif
    };

    WaylandCursorController::WaylandCursorController() : fNativeState(std::make_unique<NativeState>()) {}
    WaylandCursorController::~WaylandCursorController() = default;

    void WaylandCursorController::apply(CursorShape shape, bool visible, wl_pointer* pointer, uint32_t enterSerial,
                                        wl_compositor* compositor, wl_shm* sharedMemory, int32_t scale)
    {
        if (pointer == nullptr || enterSerial == 0)
            return;
        if (!visible)
        {
            wl_pointer_set_cursor(pointer, enterSerial, nullptr, 0, 0);
            return;
        }

    #ifdef LWS_HAS_WAYLAND_CURSOR
        if (compositor == nullptr || sharedMemory == nullptr)
            return;

        fNativeState->initialize(compositor, sharedMemory, scale);
        wl_cursor* cursor = fNativeState->selectCursor(shape, sharedMemory);
        if (fNativeState->surface == nullptr || cursor == nullptr || cursor->image_count == 0)
            return;

        wl_cursor_image* image = cursor->images[0];
        if (wl_buffer* buffer = wl_cursor_image_get_buffer(image); buffer != nullptr)
        {
            wl_pointer_set_cursor(pointer, enterSerial, fNativeState->surface,
                                  static_cast<int32_t>(image->hotspot_x) / scale,
                                  static_cast<int32_t>(image->hotspot_y) / scale);
            wl_surface_attach(fNativeState->surface, buffer, 0, 0);
            wl_surface_damage(fNativeState->surface, 0, 0, static_cast<int32_t>(image->width),
                              static_cast<int32_t>(image->height));
            wl_surface_commit(fNativeState->surface);
        }
    #endif
    }

    void WaylandCursorController::reset()
    {
        fNativeState = std::make_unique<NativeState>();
    }
}  // namespace LWS::internal

namespace LWS
{
    CursorBackendWayland::CursorBackendWayland() = default;
    CursorBackendWayland::~CursorBackendWayland() = default;

    void CursorBackendWayland::setVisible(bool visible)
    {
        if (fVisible != visible)
        {
            fVisible = visible;
            apply();
        }
    }

    void CursorBackendWayland::setCursorShape(CursorShape shape)
    {
        if (fShape != shape)
        {
            fShape = shape;
            apply();
        }
    }

    Result CursorBackendWayland::setCustomCursor(const BitmapBuffer&, Point)
    {
        // Uploading custom cursor images is not implemented by the Wayland backend.
        return Result::NotSupported;
    }

    void CursorBackendWayland::attach(WindowBackendWayland& owner)
    {
        fOwners.insert(&owner);
    }

    void CursorBackendWayland::detach(WindowBackendWayland& owner)
    {
        fOwners.erase(&owner);
    }

    void CursorBackendWayland::apply()
    {
        for (WindowBackendWayland* owner : fOwners)
            owner->applyCursor(fShape, fVisible);
    }
}  // namespace LWS

namespace LWS::internal
{
    std::unique_ptr<ICursorBackend> createDefaultCursorBackend()
    {
        return std::make_unique<CursorBackendWayland>();
    }
}  // namespace LWS::internal

#endif
