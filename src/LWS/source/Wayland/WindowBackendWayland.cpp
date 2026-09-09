#include <LWS/Wayland/WindowBackendWayland.hpp>
#include <LWS/Wayland/CursorBackendWayland.hpp>
#include <LWS/source/internal/BitmapValidation.hpp>

#ifdef LWS_PLATFORM_WAYLAND

    #include "internal/PlatformState.hpp"
    #include "internal/CaptionRenderer.hpp"
    #include "internal/WindowFrame.hpp"

    #include <algorithm>
    #include <cerrno>
    #include <cstdint>
    #include <cstring>
    #include <limits>
    #include <optional>
    #include <sys/mman.h>
    #include <sys/syscall.h>
    #include <unistd.h>
    #include <vector>

    #include <wayland-client.h>
    #include <pointer-constraints-client-protocol.h>
    #include <relative-pointer-client-protocol.h>
    #include <xdg-shell-client-protocol.h>

namespace
{
    int createAnonymousFile()
    {
    #ifdef SYS_memfd_create
        return static_cast<int>(syscall(SYS_memfd_create, "lws-wayland-buffer", 0x0001U));
    #else
        errno = ENOSYS;
        return -1;
    #endif
    }
}  // namespace

namespace LWS
{
    class WindowBackendWayland::NativeState
    {
      public:

        struct CaptionBuffer
        {
            explicit CaptionBuffer(NativeState& state) : owner(state) {}

            ~CaptionBuffer()
            {
                if (buffer != nullptr)
                    wl_buffer_destroy(buffer);
                if (mapping != MAP_FAILED)
                    munmap(mapping, mappingSize);
            }

            static void released(void* data, wl_buffer*)
            {
                auto* captionBuffer = static_cast<CaptionBuffer*>(data);
                captionBuffer->owner.releaseCaptionBuffer(captionBuffer);
            }

            NativeState& owner;
            wl_buffer* buffer = nullptr;
            void* mapping = MAP_FAILED;
            size_t mappingSize = 0;
        };

        struct PresentedBuffer
        {
            explicit PresentedBuffer(NativeState& state) : owner(state) {}
            ~PresentedBuffer()
            {
                if (buffer != nullptr)
                    wl_buffer_destroy(buffer);
                if (mapping != MAP_FAILED)
                    munmap(mapping, mappingSize);
            }
            static void released(void* data, wl_buffer*)
            {
                auto* presented = static_cast<PresentedBuffer*>(data);
                presented->owner.releasePresentedBuffer(presented);
            }

            NativeState& owner;
            wl_buffer* buffer = nullptr;
            void* mapping = MAP_FAILED;
            size_t mappingSize = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            bool busy = false;
        };

        struct PendingBitmap
        {
            std::vector<std::byte> pixels;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        struct ToplevelConfigure
        {
            Size size{};
            bool maximized = false;
            bool fullscreen = false;
        };

        explicit NativeState(WindowBackendWayland& window) : owner(window) {}

        ~NativeState()
        {
            releasePointerLock();
            releaseBuffer();
        }

        void releasePointerLock()
        {
            if (relativePointer != nullptr)
                zwp_relative_pointer_v1_destroy(relativePointer);
            if (lockedPointer != nullptr)
                zwp_locked_pointer_v1_destroy(lockedPointer);
            relativePointer = nullptr;
            lockedPointer = nullptr;
        }

        static void relativeMotion(void* data, zwp_relative_pointer_v1*, uint32_t, uint32_t, wl_fixed_t, wl_fixed_t,
                                   wl_fixed_t deltaX, wl_fixed_t deltaY)
        {
            static_cast<NativeState*>(data)->owner.handleRelativePointerMotion(wl_fixed_to_double(deltaX),
                                                                               wl_fixed_to_double(deltaY));
        }

        static void pointerLocked(void* data, zwp_locked_pointer_v1*)
        {
            static_cast<NativeState*>(data)->owner.handlePointerLockState(true);
        }

        static void pointerUnlocked(void* data, zwp_locked_pointer_v1*)
        {
            static_cast<NativeState*>(data)->owner.handlePointerLockState(false);
        }

        void releaseBuffer()
        {
            if (buffer != nullptr)
            {
                wl_buffer_destroy(buffer);
                buffer = nullptr;
            }
            if (mapping != MAP_FAILED)
            {
                munmap(mapping, mappingSize);
                mapping = MAP_FAILED;
                mappingSize = 0;
            }
            bufferReleased = true;
        }

        void releaseCaptionBuffer(CaptionBuffer* releasedBuffer)
        {
            std::erase_if(captionBuffers,
                          [releasedBuffer](const auto& buffer) { return buffer.get() == releasedBuffer; });
        }

        void releasePresentedBuffer(PresentedBuffer* releasedBuffer)
        {
            releasedBuffer->busy = false;
            if (pendingBitmap.has_value())
                owner.presentPendingBitmap();
        }

        static void surfaceConfigure(void* data, xdg_surface* surface, uint32_t serial)
        {
            auto& state = *static_cast<NativeState*>(data);
            xdg_surface_ack_configure(surface, serial);
            state.configured = true;
            if (state.pendingToplevelConfigure.has_value())
            {
                const ToplevelConfigure configure = *state.pendingToplevelConfigure;
                state.pendingToplevelConfigure.reset();
                state.owner.handleToplevelConfigure(configure.size, configure.maximized, configure.fullscreen);
            }
            if (state.owner.fEraseBackground)
                state.owner.paintBackground();
            else
                state.owner.dispatchEvent(EventPaint{});
            state.owner.paintCaption();
        }

        static void toplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states)
        {
            auto& state = *static_cast<NativeState*>(data);

            bool maximized = false;
            bool fullscreen = false;
            const auto* item = static_cast<const uint32_t*>(states->data);
            const auto* end = item + states->size / sizeof(uint32_t);
            for (; item != end; ++item)
            {
                maximized |= *item == XDG_TOPLEVEL_STATE_MAXIMIZED;
                fullscreen |= *item == XDG_TOPLEVEL_STATE_FULLSCREEN;
            }
            state.pendingToplevelConfigure = ToplevelConfigure{
                .size = {width, height},
                .maximized = maximized,
                .fullscreen = fullscreen,
            };
        }

        static void toplevelClose(void* data, xdg_toplevel*)
        {
            auto& owner = static_cast<NativeState*>(data)->owner;
            if (!owner.dispatchEvent(EventClose{}))
            {
                owner.destroy();
            }
        }

        static void toplevelConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
        static void toplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}
        static void decorationConfigure(void* data, zxdg_toplevel_decoration_v1*, uint32_t mode)
        {
            auto& state = *static_cast<NativeState*>(data);
            state.decorationMode = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                                       ? internal::WaylandDecorationMode::ServerSide
                                       : internal::WaylandDecorationMode::ClientSide;
            state.owner.updateWindowGeometry();
            state.owner.paintBackground();
            state.owner.paintCaption();
            state.owner.updateChildInputRegions();
        }

        static void bufferRelease(void* data, wl_buffer*)
        {
            auto& state = *static_cast<NativeState*>(data);
            state.bufferReleased = true;
            if (state.repaintPending)
            {
                state.repaintPending = false;
                state.owner.paintBackground();
            }
        }

        WindowBackendWayland& owner;
        wl_surface* surface = nullptr;
        xdg_surface* shellSurface = nullptr;
        xdg_toplevel* toplevel = nullptr;
        wl_subsurface* subsurface = nullptr;
        wl_surface* captionSurface = nullptr;
        wl_subsurface* captionSubsurface = nullptr;
        zxdg_toplevel_decoration_v1* decoration = nullptr;
        zwp_locked_pointer_v1* lockedPointer = nullptr;
        zwp_relative_pointer_v1* relativePointer = nullptr;
        internal::WaylandDecorationMode decorationMode = internal::WaylandDecorationMode::None;
        wl_buffer* buffer = nullptr;
        void* mapping = MAP_FAILED;
        size_t mappingSize = 0;
        bool configured = false;
        bool bufferReleased = true;
        bool repaintPending = false;
        std::optional<PendingBitmap> pendingBitmap;
        std::optional<ToplevelConfigure> pendingToplevelConfigure;
        std::vector<std::unique_ptr<CaptionBuffer>> captionBuffers;
        std::vector<std::unique_ptr<PresentedBuffer>> presentedBuffers;
        std::optional<CursorShape> frameCursorShape;
        std::optional<Rect> inputRect;
        int32_t captionWidth = 0;
    };

    WindowBackendWayland::WindowBackendWayland() = default;

    WindowBackendWayland::~WindowBackendWayland()
    {
        destroy();
        if (auto* cursor = dynamic_cast<CursorBackendWayland*>(fCursor.get()); cursor != nullptr)
            cursor->detach(*this);
    }

    Result WindowBackendWayland::create(const WindowConfig& config)
    {
        if (fNativeState != nullptr)
        {
            return Result::AlreadyCreated;
        }

        auto& platform = internal::WaylandPlatformState::current();
        if (!platform.isInitialized())
        {
            return Result::PlatformNotInitialized;
        }

        fTitle = config.title;
        fWindowStyles = config.styles;
        fPosition = config.position;
        fSize = config.size;
        fMinSize = config.minSize;
        fMaxSize = config.maxSize;
        fVisible = config.visible;
        fAlwaysOnTop = config.alwaysOnTop;
        fTransparent = config.transparent;
        fEraseBackground = config.eraseBackground;
        fDisplayState = config.displayState;

        auto native = std::make_unique<NativeState>(*this);
        native->surface = wl_compositor_create_surface(platform.compositor());
        if (native->surface == nullptr)
        {
            return Result::Failure;
        }

        if (isChildWindow())
        {
            const Result result = createSubsurface(*native);
            if (result != Result::Success)
            {
                wl_surface_destroy(native->surface);
                return result;
            }
        }
        else
        {
            native->shellSurface = xdg_wm_base_get_xdg_surface(platform.shell(), native->surface);
            native->toplevel = native->shellSurface != nullptr ? xdg_surface_get_toplevel(native->shellSurface)
                                                               : nullptr;
            if (native->shellSurface == nullptr || native->toplevel == nullptr)
            {
                if (native->toplevel != nullptr)
                    xdg_toplevel_destroy(native->toplevel);
                if (native->shellSurface != nullptr)
                    xdg_surface_destroy(native->shellSurface);
                wl_surface_destroy(native->surface);
                return Result::Failure;
            }

            if (captionMode() == internal::WaylandCaptionMode::Detached)
            {
                native->captionSurface = wl_compositor_create_surface(platform.compositor());
                native->captionSubsurface = native->captionSurface != nullptr
                                                ? wl_subcompositor_get_subsurface(platform.subcompositor(),
                                                                                  native->captionSurface,
                                                                                  native->surface)
                                                : nullptr;
                if (native->captionSurface == nullptr || native->captionSubsurface == nullptr)
                {
                    if (native->captionSubsurface != nullptr)
                        wl_subsurface_destroy(native->captionSubsurface);
                    if (native->captionSurface != nullptr)
                        wl_surface_destroy(native->captionSurface);
                    xdg_toplevel_destroy(native->toplevel);
                    xdg_surface_destroy(native->shellSurface);
                    wl_surface_destroy(native->surface);
                    return Result::Failure;
                }
                wl_subsurface_set_position(native->captionSubsurface, 0, -internal::waylandCaptionHeight);
                wl_subsurface_set_desync(native->captionSubsurface);
            }

            static constexpr xdg_surface_listener surfaceListener{.configure = NativeState::surfaceConfigure};
            static constexpr xdg_toplevel_listener toplevelListener{
                .configure = NativeState::toplevelConfigure,
                .close = NativeState::toplevelClose,
                .configure_bounds = NativeState::toplevelConfigureBounds,
                .wm_capabilities = NativeState::toplevelWmCapabilities,
            };
            xdg_surface_add_listener(native->shellSurface, &surfaceListener, native.get());
            xdg_toplevel_add_listener(native->toplevel, &toplevelListener, native.get());
        }

        fWlSurface = native->surface;
        fXdgSurface = native->shellSurface;
        fXdgToplevel = native->toplevel;
        fNativeState = std::move(native);
        platform.registerWindow(static_cast<wl_surface*>(fWlSurface), *this);
        if (fNativeState->captionSurface != nullptr)
            platform.registerWindow(fNativeState->captionSurface, *this, internal::WaylandSurfaceRole::Caption);

        if (!isChildWindow() && platform.decorationManager() != nullptr)
        {
            fNativeState->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(platform.decorationManager(),
                                                                                          fNativeState->toplevel);
            fNativeState->decorationMode = internal::WaylandDecorationMode::Pending;
            static constexpr zxdg_toplevel_decoration_v1_listener decorationListener{
                .configure = NativeState::decorationConfigure,
            };
            zxdg_toplevel_decoration_v1_add_listener(fNativeState->decoration, &decorationListener, fNativeState.get());
            setWindowStyles(WindowStyle::NoStyle, true);
        }

        if (isChildWindow())
        {
            updateSubsurfacePosition();
            updateSubsurfaceInputRegion();
            if (fVisible)
            {
                if (fEraseBackground)
                    paintBackground();
                else
                    dispatchEvent(EventPaint{});
            }
            else
            {
                wl_surface_commit(fNativeState->surface);
            }
        }
        else
        {
            setTitle(fTitle);
            setMinMaxSize(fMinSize, fMaxSize);
            setDisplayState(fDisplayState);
            wl_surface_commit(fNativeState->surface);
        }
        return Result::Success;
    }

    void WindowBackendWayland::destroy()
    {
        if (fParentBackend != nullptr)
        {
            std::erase(fParentBackend->fChildBackends, this);
            fParentBackend = nullptr;
        }
        for (WindowBackendWayland* child : fChildBackends)
            child->fParentBackend = nullptr;
        fChildBackends.clear();

        if (fNativeState == nullptr)
        {
            return;
        }

        std::ignore = setPointerLocked(false);
        auto native = std::move(fNativeState);
        if (native->captionSurface != nullptr)
            internal::WaylandPlatformState::current().unregisterWindow(native->captionSurface);
        internal::WaylandPlatformState::current().unregisterWindow(native->surface);
        native->releaseBuffer();
        if (native->decoration != nullptr)
            zxdg_toplevel_decoration_v1_destroy(native->decoration);
        if (native->subsurface != nullptr)
            wl_subsurface_destroy(native->subsurface);
        if (native->captionSubsurface != nullptr)
            wl_subsurface_destroy(native->captionSubsurface);
        if (native->captionSurface != nullptr)
            wl_surface_destroy(native->captionSurface);
        if (native->toplevel != nullptr)
            xdg_toplevel_destroy(native->toplevel);
        if (native->shellSurface != nullptr)
            xdg_surface_destroy(native->shellSurface);
        wl_surface_destroy(native->surface);
        fXdgToplevel = nullptr;
        fXdgSurface = nullptr;
        fWlSurface = nullptr;
        fVisible = false;
        dispatchEvent(EventWindowDestroyed{});
    }

    void WindowBackendWayland::show()
    {
        if (fNativeState != nullptr && !fVisible)
        {
            if (isChildWindow() && fNativeState->subsurface == nullptr)
            {
                if (createSubsurface(*fNativeState) != Result::Success)
                    return;
                fNativeState->inputRect.reset();
                updateSubsurfacePosition();
                updateSubsurfaceInputRegion();
            }
            fVisible = true;
            updateWindowGeometry();
            if (fNativeState->configured)
            {
                if (fEraseBackground)
                    paintBackground();
                else
                    dispatchEvent(EventPaint{});
                paintCaption();
            }
            else
            {
                wl_surface_commit(fNativeState->surface);
            }
        }
    }

    void WindowBackendWayland::hide()
    {
        if (fNativeState != nullptr && fVisible)
        {
            fVisible = false;
            fNativeState->pendingBitmap.reset();
            wl_surface_attach(fNativeState->surface, nullptr, 0, 0);
            wl_surface_commit(fNativeState->surface);
            if (fNativeState->captionSurface != nullptr)
            {
                wl_surface_attach(fNativeState->captionSurface, nullptr, 0, 0);
                wl_surface_commit(fNativeState->captionSurface);
            }
            if (fNativeState->subsurface != nullptr && internal::WaylandPlatformState::current().hasHostWindowFrame())
            {
                auto& platform = internal::WaylandPlatformState::current();
                wl_surface* replacementSurface = wl_compositor_create_surface(platform.compositor());
                if (replacementSurface == nullptr)
                    return;

                // WSLg's RDPRAIL shell keeps a detached subsurface visible and hit-testable after a null-buffer
                // commit. Replacing the child wl_surface removes that host surface; ordinary compositors keep the
                // stable surface and use the standard unmap path above.
                wl_subsurface_destroy(fNativeState->subsurface);
                fNativeState->subsurface = nullptr;
                platform.unregisterWindow(fNativeState->surface);
                wl_surface_destroy(fNativeState->surface);
                fNativeState->surface = replacementSurface;
                fWlSurface = replacementSurface;
                platform.registerWindow(replacementSurface, *this);
                fNativeState->inputRect.reset();
            }
            if (!isChildWindow())
                fNativeState->configured = false;
        }
    }

    bool WindowBackendWayland::getVisible() const
    {
        return fVisible;
    }

    void WindowBackendWayland::setDisplayState(WindowDisplayState state)
    {
        if (state == WindowDisplayState::Maximized && fDisplayState == WindowDisplayState::Restored && !fFullScreen &&
            !fRestoredClientSize.has_value())
        {
            fRestoredClientSize = fSize;
        }
        fDisplayState = state;
        updateChildInputRegions();
        if (fNativeState == nullptr || fNativeState->toplevel == nullptr)
        {
            return;
        }

        switch (state)
        {
            case WindowDisplayState::Minimized:
                xdg_toplevel_set_minimized(fNativeState->toplevel);
                break;
            case WindowDisplayState::Maximized:
                xdg_toplevel_set_maximized(fNativeState->toplevel);
                break;
            case WindowDisplayState::Restored:
                xdg_toplevel_unset_maximized(fNativeState->toplevel);
                break;
        }
    }

    WindowDisplayState WindowBackendWayland::getDisplayState() const
    {
        return fDisplayState;
    }

    void WindowBackendWayland::setTitle(const LWS::string_type& title)
    {
        const bool titleChanged = title != fTitle;
        fTitle = title;
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
        {
            xdg_toplevel_set_title(fNativeState->toplevel, fTitle.c_str());
        }
        if (titleChanged)
            paintCaption();
    }

    LWS::string_type WindowBackendWayland::getTitle() const
    {
        return fTitle;
    }
    void WindowBackendWayland::setWindowIcon(const std::filesystem::path&) {}
    void WindowBackendWayland::setPosition(Point position)
    {
        if (isChildWindow())
        {
            fPosition = position;
            updateSubsurfacePosition();
            updateSubsurfaceInputRegion();
        }
    }
    Point WindowBackendWayland::getPosition() const
    {
        return isChildWindow() ? fPosition : Point{};
    }

    void WindowBackendWayland::setSize(Size size)
    {
        if (size.x > 0 && size.y > 0 && size != fSize)
        {
            fSize = size;
            if (fNativeState != nullptr)
                fNativeState->pendingBitmap.reset();
            updateWindowGeometry();
            updateSubsurfaceInputRegion();
            paintBackground();
            paintCaption();
            if (fNativeState != nullptr)
                dispatchEvent(EventResize{fSize});
            updateChildInputRegions();
        }
    }

    Size WindowBackendWayland::getClientSize() const
    {
        return fSize;
    }
    Rect WindowBackendWayland::getClientRect() const
    {
        return {{0, 0}, fSize};
    }
    Size WindowBackendWayland::getWindowSize() const
    {
        return fSize;
    }

    void WindowBackendWayland::setPlacement(const WindowPlacement& placement)
    {
        const bool positionChanged = isChildWindow() && placement.position != fPosition;
        const bool sizeChanged = placement.size.x > 0 && placement.size.y > 0 && placement.size != fSize;
        if (positionChanged)
        {
            fPosition = placement.position;
            updateSubsurfacePosition();
        }
        setSize(placement.size);
        if (positionChanged && !sizeChanged)
            updateSubsurfaceInputRegion();
        if (placement.displayState != fDisplayState)
            setDisplayState(placement.displayState);
    }

    WindowPlacement WindowBackendWayland::getPlacement() const
    {
        return {getPosition(), fSize, fDisplayState};
    }

    void WindowBackendWayland::setMinMaxSize(Size minSize, Size maxSize)
    {
        fMinSize = minSize;
        fMaxSize = maxSize;
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
        {
            xdg_toplevel_set_min_size(fNativeState->toplevel, std::max(minSize.x, 0), std::max(minSize.y, 0));
            xdg_toplevel_set_max_size(fNativeState->toplevel, std::max(maxSize.x, 0), std::max(maxSize.y, 0));
        }
    }

    Size WindowBackendWayland::getMinSize() const
    {
        return fMinSize;
    }
    Size WindowBackendWayland::getMaxSize() const
    {
        return fMaxSize;
    }

    void WindowBackendWayland::setWindowStyles(WindowStyle styles, bool enable)
    {
        const auto current = std::to_underlying(fWindowStyles);
        fWindowStyles = static_cast<WindowStyle>(enable ? current | std::to_underlying(styles)
                                                        : current & ~std::to_underlying(styles));
        updateChildInputRegions();
        if (fNativeState != nullptr && fNativeState->decoration != nullptr)
        {
            const bool wantsCaption = (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::Caption)) !=
                                      0;
            if (wantsCaption)
            {
                zxdg_toplevel_decoration_v1_set_mode(fNativeState->decoration,
                                                     ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
            }
            else
            {
                zxdg_toplevel_decoration_v1_unset_mode(fNativeState->decoration);
            }
        }
        updateWindowGeometry();
        paintBackground();
        paintCaption();
    }

    WindowStyle WindowBackendWayland::getWindowStyles() const
    {
        return fWindowStyles;
    }
    void WindowBackendWayland::setForeground() {}
    void WindowBackendWayland::setFocus() {}
    bool WindowBackendWayland::isInFocus() const
    {
        return fFocused;
    }
    void WindowBackendWayland::setAlwaysOnTop(bool onTop)
    {
        fAlwaysOnTop = onTop;
    }
    bool WindowBackendWayland::getAlwaysOnTop() const
    {
        return fAlwaysOnTop;
    }

    void WindowBackendWayland::setTransparent(bool transparent)
    {
        if (fTransparent != transparent)
        {
            fTransparent = transparent;
            paintBackground();
        }
    }

    bool WindowBackendWayland::getTransparent() const
    {
        return fTransparent;
    }

    void WindowBackendWayland::setBackgroundColor(LLUtils::Color color)
    {
        fBackgroundColor = color;
        paintBackground();
    }

    LLUtils::Color WindowBackendWayland::getBackgroundColor() const
    {
        return fBackgroundColor;
    }
    void WindowBackendWayland::setEraseBackground(bool erase)
    {
        fEraseBackground = erase;
    }
    bool WindowBackendWayland::getEraseBackground() const
    {
        return fEraseBackground;
    }

    void WindowBackendWayland::handleToplevelConfigure(Size size, bool maximized, bool fullscreen)
    {
        const Size previousSize = fSize;
        const WindowDisplayState displayState = maximized ? WindowDisplayState::Maximized
                                                          : WindowDisplayState::Restored;
        const bool displayStateChanged = displayState != fDisplayState;
        const bool constrained = fullscreen || maximized;
        if (constrained && !fFullScreen && fDisplayState == WindowDisplayState::Restored &&
            !fRestoredClientSize.has_value())
        {
            fRestoredClientSize = fSize;
        }
        fDisplayState = displayState;

        fFullScreen = fullscreen;
        if (fullscreen)
        {
            fFullScreenState = FullScreenState::SingleScreen;
        }
        else if (fFullScreenState != FullScreenState::None)
        {
            fFullScreenState = FullScreenState::Windowed;
        }

        const bool hasConfiguredSize = size.x > 0 && size.y > 0;
        if (!constrained && fRestoredClientSize.has_value() && !hasConfiguredSize)
        {
            // xdg-shell allows the compositor to omit restored dimensions. Keep the last windowed client size so the
            // constrained buffer size does not become the client's fallback.
            fSize = *fRestoredClientSize;
        }
        else if (hasConfiguredSize)
        {
            const bool captionVisible = showsClientSideDecorations() || showsDetachedCaption();
            fSize = {size.x, size.y - (captionVisible ? internal::waylandCaptionHeight : 0)};
            fSize.y = std::max(fSize.y, 1);
        }
        if (!constrained)
            fRestoredClientSize.reset();

        updateWindowGeometry();
        if (displayStateChanged)
            dispatchEvent(EventDisplayStateChanged{displayState});
        if (fSize != previousSize)
            dispatchEvent(EventResize{fSize});
        updateChildInputRegions();
    }

    void WindowBackendWayland::setFullScreenState(FullScreenState state)
    {
        const bool fullscreen = state != FullScreenState::None && state != FullScreenState::Windowed;
        if (fullscreen && !fFullScreen && !fRestoredClientSize.has_value())
            fRestoredClientSize = fSize;
        fFullScreenState = state;
        fFullScreen = fullscreen;
        updateChildInputRegions();
        updateWindowGeometry();
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
        {
            if (fFullScreen)
            {
                xdg_toplevel_set_fullscreen(fNativeState->toplevel, nullptr);
                paintBackground();
                paintCaption();
            }
            else
            {
                xdg_toplevel_unset_fullscreen(fNativeState->toplevel);
            }
        }
    }

    FullScreenState WindowBackendWayland::getFullScreenState() const
    {
        return fFullScreenState;
    }
    bool WindowBackendWayland::isFullScreen() const
    {
        return fFullScreen;
    }

    void WindowBackendWayland::toggleFullScreen(bool)
    {
        setFullScreenState(fFullScreen ? FullScreenState::Windowed : FullScreenState::SingleScreen);
    }

    bool WindowBackendWayland::isMouseInClientRect() const
    {
        return fMouseInside;
    }
    bool WindowBackendWayland::isUnderMouseCursor() const
    {
        return fMouseInside;
    }
    Point WindowBackendWayland::getMousePosition() const
    {
        return fMousePosition;
    }
    void WindowBackendWayland::setLockMouseToWindowMode(LockMouseToWindowMode mode)
    {
        fLockMode = mode;
    }
    LockMouseToWindowMode WindowBackendWayland::getLockMouseToWindowMode() const
    {
        return fLockMode;
    }
    Result WindowBackendWayland::setPointerLocked(bool locked)
    {
        if (fNativeState == nullptr)
            return Result::NotCreated;
        if (locked == fPointerLockRequested)
            return Result::Success;

        auto& platform = internal::WaylandPlatformState::current();
        if (locked && (platform.pointer() == nullptr || platform.pointerConstraints() == nullptr ||
                       platform.relativePointerManager() == nullptr))
            return Result::NotSupported;

        const bool wasActive = fPointerLockActive;
        fPointerLockRequested = locked;
        fPointerLockActive = false;
        fRelativeRemainderX = 0.0;
        fRelativeRemainderY = 0.0;
        fNativeState->releasePointerLock();
        if (locked)
        {
            fNativeState->lockedPointer = zwp_pointer_constraints_v1_lock_pointer(
                platform.pointerConstraints(), fNativeState->surface, platform.pointer(), nullptr,
                ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
            fNativeState->relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(
                platform.relativePointerManager(), platform.pointer());
            if (fNativeState->lockedPointer == nullptr || fNativeState->relativePointer == nullptr)
            {
                fNativeState->releasePointerLock();
                fPointerLockRequested = false;
                return Result::Failure;
            }
            static constexpr zwp_locked_pointer_v1_listener lockListener{
                .locked = NativeState::pointerLocked,
                .unlocked = NativeState::pointerUnlocked,
            };
            static constexpr zwp_relative_pointer_v1_listener relativeListener{
                .relative_motion = NativeState::relativeMotion,
            };
            zwp_locked_pointer_v1_add_listener(fNativeState->lockedPointer, &lockListener, fNativeState.get());
            zwp_relative_pointer_v1_add_listener(fNativeState->relativePointer, &relativeListener, fNativeState.get());
        }
        else if (wasActive)
        {
            applyClientCursor();
        }
        return Result::Success;
    }
    void WindowBackendWayland::setDoubleClickMode(DoubleClickMode mode)
    {
        fDoubleClickMode = mode;
    }
    DoubleClickMode WindowBackendWayland::getDoubleClickMode() const
    {
        return fDoubleClickMode;
    }
    void WindowBackendWayland::setCursor(std::shared_ptr<ICursorBackend> cursor)
    {
        if (auto* previous = dynamic_cast<CursorBackendWayland*>(fCursor.get()); previous != nullptr)
            previous->detach(*this);
        fCursor = std::move(cursor);
        if (auto* current = dynamic_cast<CursorBackendWayland*>(fCursor.get()); current != nullptr)
            current->attach(*this);
        if (fNativeState != nullptr && fNativeState->frameCursorShape.has_value())
            internal::WaylandPlatformState::current().applyCursor(*this, *fNativeState->frameCursorShape, true);
        else
            applyClientCursor();
    }

    void WindowBackendWayland::setParent(IWindowBackend* parent)
    {
        if (fParentBackend != nullptr)
            std::erase(fParentBackend->fChildBackends, this);
        fParentBackend = dynamic_cast<WindowBackendWayland*>(parent);
        if (fParentBackend != nullptr)
            fParentBackend->fChildBackends.push_back(this);
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
        {
            xdg_toplevel_set_parent(fNativeState->toplevel,
                                    fParentBackend != nullptr && fParentBackend->fNativeState != nullptr
                                        ? fParentBackend->fNativeState->toplevel
                                        : nullptr);
        }
        updateSubsurfacePosition();
        updateSubsurfaceInputRegion();
    }

    Result WindowBackendWayland::enableDragAndDrop(bool enable)
    {
        auto& platform = internal::WaylandPlatformState::current();
        if (!platform.isInitialized())
            return Result::PlatformNotInitialized;
        if (enable && !platform.supportsDragAndDrop())
            return Result::NotSupported;
        fDragAndDropEnabled = enable;
        return Result::Success;
    }

    EventListenerToken WindowBackendWayland::addListener(EventCallback callback)
    {
        const EventListenerToken token = fNextListenerToken++;
        fListeners.emplace_back(token, std::move(callback));
        return token;
    }

    void WindowBackendWayland::removeListener(EventListenerToken token)
    {
        std::erase_if(fListeners, [token](const auto& listener) { return listener.first == token; });
    }

    void WindowBackendWayland::injectRawEvent(void* platformEvent)
    {
        dispatchEvent(EventRawPlatform{std::to_underlying(BackendId::Wayland), platformEvent});
    }

    Result WindowBackendWayland::presentBitmap(const BitmapBuffer& bitmap)
    {
        if (fNativeState == nullptr)
            return Result::NotCreated;
        if (!fVisible || !fNativeState->configured)
            return Result::InvalidState;
        if (fSize.x <= 0 || fSize.y <= 0 || fSize.x > std::numeric_limits<int32_t>::max() / 4)
            return Result::Failure;
        const auto layout = internal::validateBitmapBuffer(bitmap);
        const size_t tightPitch = static_cast<size_t>(fSize.x) * 4U;
        if (!layout.has_value() || bitmap.format != BitmapPixelFormat::Bgra8Premultiplied ||
            bitmap.rowOrder != BitmapRowOrder::TopDown || bitmap.width != static_cast<uint32_t>(fSize.x) ||
            bitmap.height != static_cast<uint32_t>(fSize.y) || layout->rowPitch != tightPitch)
        {
            return Result::Failure;
        }

        const size_t bufferSize = tightPitch * bitmap.height;
        auto& presentedBuffers = fNativeState->presentedBuffers;
        std::erase_if(
            presentedBuffers, [&](const auto& presented)
            { return !presented->busy && (presented->width != bitmap.width || presented->height != bitmap.height); });
        const auto available = std::ranges::find_if(presentedBuffers,
                                                    [](const auto& presented) { return !presented->busy; });

        // Bound compositor-owned buffers and coalesce later paints until one is released.
        constexpr size_t maxPresentedBuffers = 3;
        if (available == presentedBuffers.end() && presentedBuffers.size() >= maxPresentedBuffers)
        {
            auto& pending = fNativeState->pendingBitmap;
            if (!pending.has_value() || pending->pixels.size() != bufferSize)
                pending.emplace(NativeState::PendingBitmap{.pixels = std::vector<std::byte>(bufferSize)});
            pending->width = bitmap.width;
            pending->height = bitmap.height;
            std::memcpy(pending->pixels.data(), bitmap.pixels.data(), bufferSize);
            return Result::Success;
        }

        NativeState::PresentedBuffer* presented = nullptr;
        if (available != presentedBuffers.end())
        {
            presented = available->get();
        }
        else
        {
            const int fd = createAnonymousFile();
            if (fd < 0 || bufferSize > static_cast<size_t>(std::numeric_limits<off_t>::max()) ||
                bufferSize > static_cast<size_t>(std::numeric_limits<int32_t>::max()) ||
                ftruncate(fd, static_cast<off_t>(bufferSize)) != 0)
            {
                if (fd >= 0)
                    close(fd);
                return Result::Failure;
            }

            void* mapping = mmap(nullptr, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
            if (mapping == MAP_FAILED)
            {
                close(fd);
                return Result::Failure;
            }

            wl_shm_pool* pool = wl_shm_create_pool(internal::WaylandPlatformState::current().sharedMemory(), fd,
                                                   static_cast<int32_t>(bufferSize));
            wl_buffer* buffer = pool != nullptr ? wl_shm_pool_create_buffer(pool, 0, fSize.x, fSize.y,
                                                                            static_cast<int32_t>(tightPitch),
                                                                            WL_SHM_FORMAT_ARGB8888)
                                                : nullptr;
            if (pool != nullptr)
                wl_shm_pool_destroy(pool);
            close(fd);
            if (buffer == nullptr)
            {
                munmap(mapping, bufferSize);
                return Result::Failure;
            }

            auto newPresented = std::make_unique<NativeState::PresentedBuffer>(*fNativeState);
            newPresented->buffer = buffer;
            newPresented->mapping = mapping;
            newPresented->mappingSize = bufferSize;
            newPresented->width = bitmap.width;
            newPresented->height = bitmap.height;
            static constexpr wl_buffer_listener bufferListener{.release = NativeState::PresentedBuffer::released};
            wl_buffer_add_listener(buffer, &bufferListener, newPresented.get());
            presented = newPresented.get();
            presentedBuffers.push_back(std::move(newPresented));
        }

        std::memcpy(presented->mapping, bitmap.pixels.data(), bufferSize);
        presented->busy = true;
        wl_surface_attach(fNativeState->surface, presented->buffer, 0, 0);
        if (wl_surface_get_version(fNativeState->surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
            wl_surface_damage_buffer(fNativeState->surface, 0, 0, fSize.x, fSize.y);
        else
            wl_surface_damage(fNativeState->surface, 0, 0, fSize.x, fSize.y);
        wl_surface_commit(fNativeState->surface);
        return Result::Success;
    }

    void WindowBackendWayland::presentPendingBitmap()
    {
        NativeState::PendingBitmap pending = std::move(*fNativeState->pendingBitmap);
        fNativeState->pendingBitmap.reset();
        const BitmapBuffer bitmap{
            .pixels = pending.pixels,
            .format = BitmapPixelFormat::Bgra8Premultiplied,
            .rowOrder = BitmapRowOrder::TopDown,
            .width = pending.width,
            .height = pending.height,
            .rowPitch = 0,
        };
        std::ignore = presentBitmap(bitmap);
    }

    Handle WindowBackendWayland::getHandle() const
    {
        return reinterpret_cast<Handle>(fWlSurface);
    }

    void WindowBackendWayland::handlePointerEnter(Point position, internal::WaylandSurfaceRole surfaceRole)
    {
        fMouseInside = true;
        fMousePosition = position;
        const internal::WaylandFrameHit hit = frameHit(position, surfaceRole);
        if (hit.action == internal::WaylandFrameAction::Client)
            applyClientCursor();
        else
            applyFrameCursor(hit);
    }

    void WindowBackendWayland::handlePointerLeave()
    {
        fMouseInside = false;
        if (fNativeState != nullptr)
            fNativeState->frameCursorShape.reset();
    }

    void WindowBackendWayland::handlePointerMotion(Point position, Point delta,
                                                   internal::WaylandSurfaceRole surfaceRole)
    {
        if (fPointerLockActive)
            return;
        fMousePosition = position;
        const internal::WaylandFrameHit hit = frameHit(position, surfaceRole);
        applyFrameCursor(hit);
        if (hit.action != internal::WaylandFrameAction::Client)
            return;

        position -= contentOffset();
        fMousePosition = position;
        dispatchEvent(EventMouseMove{position, delta});
    }

    void WindowBackendWayland::handlePointerLockState(bool active)
    {
        if (fPointerLockActive == active)
            return;
        fPointerLockActive = active;
        fRelativeRemainderX = 0.0;
        fRelativeRemainderY = 0.0;
        if (fNativeState != nullptr)
        {
            if (active)
                applyCursor(CursorShape::Arrow, false);
            else
                applyClientCursor();
        }
    }

    void WindowBackendWayland::handleRelativePointerMotion(double deltaX, double deltaY)
    {
        if (!fPointerLockActive)
            return;
        fRelativeRemainderX += deltaX;
        fRelativeRemainderY += deltaY;
        const Point delta{static_cast<int32_t>(fRelativeRemainderX), static_cast<int32_t>(fRelativeRemainderY)};
        fRelativeRemainderX -= delta.x;
        fRelativeRemainderY -= delta.y;
        if (delta != Point{})
            dispatchEvent(EventMouseMove{fMousePosition, delta});
    }

    void WindowBackendWayland::handlePointerButton(MouseButton button, bool pressed, Point position,
                                                   internal::WaylandSurfaceRole surfaceRole, uint32_t time)
    {
        const internal::WaylandFrameHit hit = frameHit(position, surfaceRole);
        if (hit.action != internal::WaylandFrameAction::Client)
        {
            if (button == MouseButton::Left && pressed)
            {
                if (hit.action != internal::WaylandFrameAction::Move)
                    fCaptionClickPending = false;
                switch (hit.action)
                {
                    case internal::WaylandFrameAction::Close:
                        if (!dispatchEvent(EventClose{}))
                            destroy();
                        break;
                    case internal::WaylandFrameAction::Move:
                    {
                        if (fDoubleClickMode == DoubleClickMode::Default && isCaptionDoubleClick(time, position))
                        {
                            setDisplayState(fDisplayState == WindowDisplayState::Maximized
                                                ? WindowDisplayState::Restored
                                                : WindowDisplayState::Maximized);
                        }
                        else if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
                        {
                            auto& platform = internal::WaylandPlatformState::current();
                            xdg_toplevel_move(fNativeState->toplevel, platform.seat(), platform.pointerButtonSerial());
                        }
                        break;
                    }
                    case internal::WaylandFrameAction::Resize:
                        std::ignore = beginResize(hit.edge);
                        break;
                    case internal::WaylandFrameAction::Client:
                        break;
                }
            }
            return;
        }

        if (button == MouseButton::Left && pressed)
            fCaptionClickPending = false;
        position -= contentOffset();
        fMousePosition = position;
        dispatchEvent(EventMouseButton{button, pressed, position});
    }

    bool WindowBackendWayland::isCaptionDoubleClick(uint32_t time, Point position)
    {
        constexpr uint32_t DoubleClickTimeMilliseconds = 500;
        constexpr int32_t DoubleClickRadiusSquared = 25;
        const bool doubleClick = fCaptionClickPending && time - fLastCaptionClickTime <= DoubleClickTimeMilliseconds &&
                                 position.DistanceSquared(fLastCaptionClickPosition) <= DoubleClickRadiusSquared;
        fCaptionClickPending = !doubleClick;
        fLastCaptionClickTime = time;
        fLastCaptionClickPosition = position;
        return doubleClick;
    }

    void WindowBackendWayland::handlePointerWheel(int32_t delta, Point position)
    {
        dispatchEvent(EventMouseWheel{delta, position});
    }

    void WindowBackendWayland::handleKeyboardFocus(bool focused)
    {
        if (fFocused != focused)
        {
            fFocused = focused;
            dispatchEvent(focused ? AnyEvent{EventFocusGained{}} : AnyEvent{EventFocusLost{}});
        }
    }

    void WindowBackendWayland::handleKey(KeyCode key, bool pressed, bool repeat)
    {
        if (key != KeyCode::Unknown)
        {
            dispatchEvent(pressed ? AnyEvent{EventKeyDown{key, repeat}} : AnyEvent{EventKeyUp{key}});
        }
    }

    void WindowBackendWayland::setAppId(const std::string& appId)
    {
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
        {
            xdg_toplevel_set_app_id(fNativeState->toplevel, appId.c_str());
        }
    }

    WindowBackendWayland* WindowBackendWayland::dragDropTarget()
    {
        WindowBackendWayland* window = this;
        while (window != nullptr && !window->fDragAndDropEnabled)
            window = window->fParentBackend;
        return window;
    }

    bool WindowBackendWayland::dispatchEvent(const AnyEvent& event)
    {
        std::vector<EventListenerToken> tokens;
        tokens.reserve(fListeners.size());
        for (const auto& [token, callback] : fListeners)
        {
            tokens.push_back(token);
        }

        bool handled = false;
        for (EventListenerToken token : tokens)
        {
            const auto item = std::ranges::find(fListeners, token, &decltype(fListeners)::value_type::first);
            if (item != fListeners.end() && item->second(event))
            {
                handled = true;
                break;
            }
        }
        return handled;
    }

    void WindowBackendWayland::applyCursor(CursorShape shape, bool visible)
    {
        if (fNativeState == nullptr || !fNativeState->frameCursorShape.has_value())
            internal::WaylandPlatformState::current().applyCursor(*this, shape, visible);
    }

    void WindowBackendWayland::applyClientCursor()
    {
        if (auto* waylandCursor = dynamic_cast<CursorBackendWayland*>(fCursor.get()); waylandCursor != nullptr)
            waylandCursor->apply();
        else
            internal::WaylandPlatformState::current().applyCursor(*this, CursorShape::Arrow, true);
    }

    void WindowBackendWayland::applyFrameCursor(const internal::WaylandFrameHit& hit)
    {
        if (fNativeState == nullptr)
            return;

        std::optional<CursorShape> shape;
        switch (hit.edge)
        {
            case internal::WaylandResizeEdge::Top:
            case internal::WaylandResizeEdge::Bottom:
                shape = CursorShape::SizeNS;
                break;
            case internal::WaylandResizeEdge::Left:
            case internal::WaylandResizeEdge::Right:
                shape = CursorShape::SizeEW;
                break;
            case internal::WaylandResizeEdge::TopLeft:
            case internal::WaylandResizeEdge::BottomRight:
                shape = CursorShape::SizeNWSE;
                break;
            case internal::WaylandResizeEdge::TopRight:
            case internal::WaylandResizeEdge::BottomLeft:
                shape = CursorShape::SizeNESW;
                break;
            case internal::WaylandResizeEdge::None:
                if (hit.action != internal::WaylandFrameAction::Client)
                    shape = CursorShape::Arrow;
                break;
        }

        if (shape.has_value())
        {
            if (fNativeState->frameCursorShape != shape)
            {
                fNativeState->frameCursorShape = shape;
                internal::WaylandPlatformState::current().applyCursor(*this, *shape, true);
            }
        }
        else if (fNativeState->frameCursorShape.has_value())
        {
            fNativeState->frameCursorShape.reset();
            applyClientCursor();
        }
    }

    bool WindowBackendWayland::beginResize(internal::WaylandResizeEdge edge)
    {
        if (edge == internal::WaylandResizeEdge::None || !canResize())
            return false;

        auto& platform = internal::WaylandPlatformState::current();
        if (platform.seat() == nullptr || platform.pointerButtonSerial() == 0)
            return false;

        xdg_toplevel_resize(fNativeState->toplevel, platform.seat(), platform.pointerButtonSerial(),
                            static_cast<xdg_toplevel_resize_edge>(std::to_underlying(edge)));
        return true;
    }

    bool WindowBackendWayland::canResize() const
    {
        return fNativeState != nullptr && fNativeState->toplevel != nullptr &&
               internal::isWaylandResizeEnabled(fWindowStyles, fDisplayState, fFullScreen, isChildWindow());
    }

    Result WindowBackendWayland::createSubsurface(NativeState& nativeState)
    {
        auto& platform = internal::WaylandPlatformState::current();
        if (platform.subcompositor() == nullptr)
            return Result::NotSupported;
        if (fParentBackend == nullptr || fParentBackend->fNativeState == nullptr)
            return Result::InvalidState;

        nativeState.subsurface = wl_subcompositor_get_subsurface(platform.subcompositor(), nativeState.surface,
                                                                 fParentBackend->fNativeState->surface);
        if (nativeState.subsurface == nullptr)
            return Result::Failure;
        wl_subsurface_set_desync(nativeState.subsurface);
        nativeState.configured = true;
        return Result::Success;
    }

    Point WindowBackendWayland::contentOffset() const
    {
        return {0, showsClientSideDecorations() ? internal::waylandCaptionHeight : 0};
    }

    internal::WaylandCaptionMode WindowBackendWayland::captionMode() const
    {
        const bool captionStyle = (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::Caption)) != 0;
        return internal::waylandCaptionMode(isChildWindow(),
                                            internal::WaylandPlatformState::current().hasHostWindowFrame(),
                                            decorationMode(), captionStyle);
    }

    internal::WaylandDecorationMode WindowBackendWayland::decorationMode() const
    {
        if (fNativeState != nullptr && fNativeState->decoration != nullptr)
            return fNativeState->decorationMode;
        return internal::WaylandPlatformState::current().decorationManager() == nullptr
                   ? internal::WaylandDecorationMode::None
                   : internal::WaylandDecorationMode::Pending;
    }

    internal::WaylandFrameHit WindowBackendWayland::frameHit(Point position,
                                                             internal::WaylandSurfaceRole surfaceRole) const
    {
        const bool clientDecorations = showsClientSideDecorations();
        const bool detachedCaption = showsDetachedCaption();
        const bool onCaption = surfaceRole == internal::WaylandSurfaceRole::Caption;
        if ((onCaption && !detachedCaption) || (!onCaption && !clientDecorations && !detachedCaption))
            return {};

        const Size surfaceSize = onCaption ? Size{fNativeState->captionWidth, internal::waylandCaptionHeight}
                                           : Size{fSize.x,
                                                  fSize.y + (clientDecorations ? internal::waylandCaptionHeight : 0)};
        const bool closeButton = (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::CloseButton)) !=
                                 0;
        return internal::waylandFrameHit(position, surfaceSize,
                                         {
                                             .surface = surfaceRole,
                                             .detachedCaption = detachedCaption,
                                             .closeButton = closeButton,
                                             .resizeEnabled = canResize(),
                                         });
    }

    bool WindowBackendWayland::isChildWindow() const
    {
        return (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::ChildWindow)) != 0;
    }

    void WindowBackendWayland::updateSubsurfacePosition()
    {
        if (fNativeState != nullptr && fNativeState->subsurface != nullptr)
        {
            const Point parentOffset = fParentBackend != nullptr ? fParentBackend->contentOffset() : Point{};
            wl_subsurface_set_position(fNativeState->subsurface, fPosition.x + parentOffset.x,
                                       fPosition.y + parentOffset.y);
        }
    }

    void WindowBackendWayland::updateSubsurfaceInputRegion()
    {
        if (fNativeState == nullptr || fNativeState->subsurface == nullptr)
            return;

        // Subsurfaces otherwise consume pointer events over client-side frame edges. Restrict only their input
        // region so the parent can start a resize without shrinking or clipping the child visually.
        const bool preserveParentResizeFrame = fParentBackend != nullptr && fParentBackend->canResize() &&
                                               (fParentBackend->showsClientSideDecorations() ||
                                                fParentBackend->showsDetachedCaption());
        const Size parentSize = fParentBackend != nullptr ? fParentBackend->fSize : Size{};
        const Rect inputRect = internal::waylandChildInputRect(fPosition, fSize, parentSize, preserveParentResizeFrame);
        const Point topLeft = inputRect.GetCorner(LLUtils::TopLeft);
        const Point bottomRight = inputRect.GetCorner(LLUtils::BottomRight);
        if (fNativeState->inputRect.has_value() && fNativeState->inputRect->GetCorner(LLUtils::TopLeft) == topLeft &&
            fNativeState->inputRect->GetCorner(LLUtils::BottomRight) == bottomRight)
        {
            return;
        }

        wl_region* region = wl_compositor_create_region(internal::WaylandPlatformState::current().compositor());
        if (region == nullptr)
            return;
        if (!inputRect.IsEmpty())
            wl_region_add(region, topLeft.x, topLeft.y, inputRect.GetWidth(), inputRect.GetHeight());
        wl_surface_set_input_region(fNativeState->surface, region);
        wl_region_destroy(region);
        wl_surface_commit(fNativeState->surface);
        fNativeState->inputRect = inputRect;
    }

    void WindowBackendWayland::updateChildInputRegions()
    {
        for (WindowBackendWayland* child : fChildBackends)
            child->updateSubsurfaceInputRegion();
    }

    bool WindowBackendWayland::showsClientSideDecorations() const
    {
        return !fFullScreen && captionMode() == internal::WaylandCaptionMode::ClientSide;
    }

    bool WindowBackendWayland::showsDetachedCaption() const
    {
        return !fFullScreen && captionMode() == internal::WaylandCaptionMode::Detached;
    }

    void WindowBackendWayland::updateWindowGeometry()
    {
        if (fNativeState == nullptr || fNativeState->shellSurface == nullptr || fSize.x <= 0 || fSize.y <= 0)
            return;

        const bool clientCaptionVisible = showsClientSideDecorations();
        const bool detachedCaptionVisible = fVisible && showsDetachedCaption();
        const int32_t captionHeight = clientCaptionVisible || detachedCaptionVisible ? internal::waylandCaptionHeight
                                                                                     : 0;
        xdg_surface_set_window_geometry(fNativeState->shellSurface, 0,
                                        detachedCaptionVisible ? -internal::waylandCaptionHeight : 0, fSize.x,
                                        fSize.y + captionHeight);
    }

    void WindowBackendWayland::paintBackground()
    {
        if (fNativeState == nullptr || !fVisible || !fNativeState->configured || !fEraseBackground || fSize.x <= 0 ||
            fSize.y <= 0)
        {
            return;
        }
        if (!fNativeState->bufferReleased)
        {
            fNativeState->repaintPending = true;
            return;
        }

        fNativeState->releaseBuffer();
        constexpr size_t bytesPerPixel = 4;
        const size_t width = static_cast<size_t>(fSize.x);
        const int32_t bufferHeight = fSize.y + (showsClientSideDecorations() ? internal::waylandCaptionHeight : 0);
        const size_t height = static_cast<size_t>(bufferHeight);
        if (width > std::numeric_limits<size_t>::max() / bytesPerPixel / height)
        {
            return;
        }
        const size_t stride = width * bytesPerPixel;
        const size_t bufferSize = stride * height;
        if (bufferSize > static_cast<size_t>(std::numeric_limits<off_t>::max()))
        {
            return;
        }

        const int fd = createAnonymousFile();
        if (fd < 0 || ftruncate(fd, static_cast<off_t>(bufferSize)) != 0)
        {
            if (fd >= 0)
                close(fd);
            return;
        }

        void* mapping = mmap(nullptr, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED)
        {
            close(fd);
            return;
        }

        wl_shm_pool* pool = wl_shm_create_pool(internal::WaylandPlatformState::current().sharedMemory(), fd,
                                               static_cast<int32_t>(bufferSize));
        wl_buffer* buffer = pool != nullptr
                                ? wl_shm_pool_create_buffer(pool, 0, fSize.x, bufferHeight,
                                                            static_cast<int32_t>(stride), WL_SHM_FORMAT_ARGB8888)
                                : nullptr;
        if (pool != nullptr)
            wl_shm_pool_destroy(pool);
        close(fd);
        if (buffer == nullptr)
        {
            munmap(mapping, bufferSize);
            return;
        }

        const uint32_t alpha = fTransparent ? fBackgroundColor.A() : 0xffU;
        const uint32_t pixel = alpha << 24U | static_cast<uint32_t>(fBackgroundColor.R()) << 16U |
                               static_cast<uint32_t>(fBackgroundColor.G()) << 8U | fBackgroundColor.B();
        std::fill_n(static_cast<uint32_t*>(mapping), width * height, pixel);
        if (showsClientSideDecorations())
        {
            auto* pixels = static_cast<uint32_t*>(mapping);
            constexpr uint32_t captionColor = 0xff303030U;
            constexpr uint32_t closeColor = 0xffc42b1cU;
            constexpr uint32_t glyphColor = 0xffffffffU;
            std::fill_n(pixels, width * internal::waylandCaptionHeight, captionColor);
            if ((std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::CloseButton)) != 0)
            {
                const int32_t closeLeft = std::max(fSize.x - internal::waylandCloseButtonWidth, 0);
                for (int32_t y = 0; y < internal::waylandCaptionHeight; ++y)
                    std::fill_n(pixels + static_cast<size_t>(y) * width + closeLeft, fSize.x - closeLeft, closeColor);
                const int32_t centerX = closeLeft + (fSize.x - closeLeft) / 2;
                const int32_t centerY = internal::waylandCaptionHeight / 2;
                for (int32_t offset = -5; offset <= 5; ++offset)
                {
                    pixels[static_cast<size_t>(centerY + offset) * width + centerX + offset] = glyphColor;
                    pixels[static_cast<size_t>(centerY + offset) * width + centerX - offset] = glyphColor;
                }
            }
        }
        fNativeState->buffer = buffer;
        fNativeState->mapping = mapping;
        fNativeState->mappingSize = bufferSize;
        fNativeState->bufferReleased = false;
        static constexpr wl_buffer_listener bufferListener{.release = NativeState::bufferRelease};
        wl_buffer_add_listener(buffer, &bufferListener, fNativeState.get());
        wl_surface_attach(fNativeState->surface, buffer, 0, 0);
        if (wl_surface_get_version(fNativeState->surface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
        {
            wl_surface_damage_buffer(fNativeState->surface, 0, 0, fSize.x, bufferHeight);
        }
        else
        {
            wl_surface_damage(fNativeState->surface, 0, 0, fSize.x, bufferHeight);
        }
        wl_surface_commit(fNativeState->surface);
        dispatchEvent(EventPaint{});
    }

    void WindowBackendWayland::paintCaption()
    {
        if (fNativeState == nullptr || fNativeState->captionSurface == nullptr ||
            fNativeState->shellSurface == nullptr || !fNativeState->configured)
        {
            return;
        }

        const bool visible = fVisible && showsDetachedCaption() && fSize.x > 0;
        if (!visible)
        {
            wl_surface_attach(fNativeState->captionSurface, nullptr, 0, 0);
            wl_surface_commit(fNativeState->captionSurface);
            fNativeState->captionWidth = 0;
            return;
        }

        constexpr size_t bytesPerPixel = 4;
        const size_t width = static_cast<size_t>(fSize.x);
        const size_t stride = width * bytesPerPixel;
        const size_t bufferSize = stride * internal::waylandCaptionHeight;
        const int fd = createAnonymousFile();
        if (fd < 0 || bufferSize > static_cast<size_t>(std::numeric_limits<off_t>::max()) ||
            ftruncate(fd, static_cast<off_t>(bufferSize)) != 0)
        {
            if (fd >= 0)
                close(fd);
            return;
        }

        void* mapping = mmap(nullptr, bufferSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mapping == MAP_FAILED)
        {
            close(fd);
            return;
        }

        wl_shm_pool* pool = wl_shm_create_pool(internal::WaylandPlatformState::current().sharedMemory(), fd,
                                               static_cast<int32_t>(bufferSize));
        wl_buffer* buffer = pool != nullptr
                                ? wl_shm_pool_create_buffer(pool, 0, fSize.x, internal::waylandCaptionHeight,
                                                            static_cast<int32_t>(stride), WL_SHM_FORMAT_ARGB8888)
                                : nullptr;
        if (pool != nullptr)
            wl_shm_pool_destroy(pool);
        close(fd);
        if (buffer == nullptr)
        {
            munmap(mapping, bufferSize);
            return;
        }

        auto* pixels = static_cast<uint32_t*>(mapping);
        constexpr uint32_t captionColor = 0xff303030U;
        constexpr uint32_t closeColor = 0xffc42b1cU;
        constexpr uint32_t glyphColor = 0xffffffffU;
        std::fill_n(pixels, width * internal::waylandCaptionHeight, captionColor);
        const bool closeButton = (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::CloseButton)) !=
                                 0;
        const int32_t titleRight = closeButton ? std::max(fSize.x - internal::waylandCloseButtonWidth, 0) : fSize.x;
        internal::renderCaptionTitle({pixels, width * internal::waylandCaptionHeight}, fSize.x, fTitle, titleRight);
        if (closeButton)
        {
            const int32_t closeLeft = std::max(fSize.x - internal::waylandCloseButtonWidth, 0);
            for (int32_t y = 0; y < internal::waylandCaptionHeight; ++y)
                std::fill_n(pixels + static_cast<size_t>(y) * width + closeLeft, fSize.x - closeLeft, closeColor);
            const int32_t centerX = closeLeft + (fSize.x - closeLeft) / 2;
            const int32_t centerY = internal::waylandCaptionHeight / 2;
            for (int32_t offset = -5; offset <= 5; ++offset)
            {
                pixels[static_cast<size_t>(centerY + offset) * width + centerX + offset] = glyphColor;
                pixels[static_cast<size_t>(centerY + offset) * width + centerX - offset] = glyphColor;
            }
        }

        auto captionBuffer = std::make_unique<NativeState::CaptionBuffer>(*fNativeState);
        captionBuffer->buffer = buffer;
        captionBuffer->mapping = mapping;
        captionBuffer->mappingSize = bufferSize;
        static constexpr wl_buffer_listener bufferListener{.release = NativeState::CaptionBuffer::released};
        wl_buffer_add_listener(buffer, &bufferListener, captionBuffer.get());
        wl_surface_attach(fNativeState->captionSurface, buffer, 0, 0);
        if (wl_surface_get_version(fNativeState->captionSurface) >= WL_SURFACE_DAMAGE_BUFFER_SINCE_VERSION)
            wl_surface_damage_buffer(fNativeState->captionSurface, 0, 0, fSize.x, internal::waylandCaptionHeight);
        else
            wl_surface_damage(fNativeState->captionSurface, 0, 0, fSize.x, internal::waylandCaptionHeight);
        wl_surface_commit(fNativeState->captionSurface);
        fNativeState->captionWidth = fSize.x;
        fNativeState->captionBuffers.push_back(std::move(captionBuffer));
    }
}  // namespace LWS

#endif
