#include <LWS/source/Wayland/internal/WindowBackendWayland.hpp>
#include <LWS/source/Wayland/internal/CursorBackendWayland.hpp>
#include <LWS/source/internal/BitmapValidation.hpp>
#include <LWS/Window.hpp>

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
            captionBuffers.clear();
            presentedBuffers.clear();
            pendingBitmap.reset();
            if (transparentBuffer != nullptr)
                wl_buffer_destroy(transparentBuffer);
            if (decoration != nullptr)
                zxdg_toplevel_decoration_v1_destroy(decoration);
            if (fractionalScale != nullptr)
                wp_fractional_scale_v1_destroy(fractionalScale);
            if (viewport != nullptr)
                wp_viewport_destroy(viewport);
            if (captionViewport != nullptr)
                wp_viewport_destroy(captionViewport);
            if (subsurface != nullptr)
                wl_subsurface_destroy(subsurface);
            if (captionSubsurface != nullptr)
                wl_subsurface_destroy(captionSubsurface);
            if (captionSurface != nullptr)
                wl_surface_destroy(captionSurface);
            if (toplevel != nullptr)
                xdg_toplevel_destroy(toplevel);
            if (shellSurface != nullptr)
                xdg_surface_destroy(shellSurface);
            if (surface != nullptr)
                wl_surface_destroy(surface);
        }

        wl_buffer* getTransparentBuffer()
        {
            if (transparentBuffer != nullptr)
                return transparentBuffer;

            constexpr size_t bufferSize = sizeof(uint32_t);
            const int fd = createAnonymousFile();
            if (fd < 0 || ftruncate(fd, bufferSize) != 0)
            {
                if (fd >= 0)
                    close(fd);
                return nullptr;
            }
            // ftruncate zero-fills the new storage, yielding one transparent ARGB pixel.
            wl_shm_pool* pool = wl_shm_create_pool(owner.fPlatform.sharedMemory(), fd, bufferSize);
            transparentBuffer = pool != nullptr
                                    ? wl_shm_pool_create_buffer(pool, 0, 1, 1, bufferSize, WL_SHM_FORMAT_ARGB8888)
                                    : nullptr;
            if (pool != nullptr)
                wl_shm_pool_destroy(pool);
            close(fd);
            return transparentBuffer;
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
            WindowBackendWayland& owner = state.owner;
            xdg_surface_ack_configure(surface, serial);
            state.configured = true;
            if (state.pendingToplevelConfigure.has_value())
            {
                const ToplevelConfigure configure = *state.pendingToplevelConfigure;
                state.pendingToplevelConfigure.reset();
                state.owner.handleToplevelConfigure(configure.size, configure.maximized, configure.fullscreen);
            }
            if (owner.fNativeState == nullptr)
                return;
            if (owner.fEraseBackground)
                owner.paintBackground();
            else
                owner.dispatchEvent(EventPaint{});
            owner.paintCaption();
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
            if (owner.dispatchEvent(EventCloseRequested{}) == EventResponse::Unhandled)
            {
                std::ignore = owner.owner().Destroy();
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
            WindowBackendWayland& owner = state.owner;
            owner.updateWindowGeometry();
            owner.paintBackground();
            owner.paintCaption();
            owner.updateChildInputRegions();
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

        static void surfaceEnter(void* data, wl_surface*, wl_output* output)
        {
            auto& state = *static_cast<NativeState*>(data);
            if (std::ranges::find(state.outputs, output) == state.outputs.end())
                state.outputs.push_back(output);
            state.owner.updateContentScale();
        }

        static void surfaceLeave(void* data, wl_surface*, wl_output* output)
        {
            auto& state = *static_cast<NativeState*>(data);
            std::erase(state.outputs, output);
            state.owner.updateContentScale();
        }

        static void preferredBufferScale(void* data, wl_surface*, int32_t scale)
        {
            auto& state = *static_cast<NativeState*>(data);
            state.preferredBufferScaleValue = std::max(scale, 1);
            state.owner.updateContentScale();
        }

        static void preferredBufferTransform(void*, wl_surface*, uint32_t) {}

        static void preferredScale(void* data, wp_fractional_scale_v1*, uint32_t scale)
        {
            static_cast<NativeState*>(data)->owner.setContentScale(static_cast<double>(scale) / 120.0);
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
        std::vector<wl_output*> outputs;
        double scale = 1.0;
        std::optional<int32_t> preferredBufferScaleValue;
        wp_fractional_scale_v1* fractionalScale = nullptr;
        wp_viewport* viewport = nullptr;
        wp_viewport* captionViewport = nullptr;
        wl_buffer* transparentBuffer = nullptr;
    };

    WindowBackendWayland::WindowBackendWayland(Window& owner, internal::WaylandPlatformState& platform)
        : internal::IWindowBackend(owner), fPlatform(platform)
    {
    }

    WindowBackendWayland::~WindowBackendWayland()
    {
        destroy();
        if (auto* cursor = dynamic_cast<CursorBackendWayland*>(fCursor.get()); cursor != nullptr)
            cursor->detach(*this);
    }

    Result WindowBackendWayland::create(const internal::NativeWindowConfig& config)
    {
        if (fNativeState != nullptr)
        {
            return Result::AlreadyCreated;
        }

        auto& platform = fPlatform;
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
        static constexpr wl_surface_listener surfaceListener{
            .enter = NativeState::surfaceEnter,
            .leave = NativeState::surfaceLeave,
            .preferred_buffer_scale = NativeState::preferredBufferScale,
            .preferred_buffer_transform = NativeState::preferredBufferTransform,
        };
        wl_surface_add_listener(native->surface, &surfaceListener, native.get());
        if (platform.fractionalScaleManager() != nullptr && platform.viewporter() != nullptr)
        {
            native->fractionalScale = wp_fractional_scale_manager_v1_get_fractional_scale(
                platform.fractionalScaleManager(), native->surface);
            native->viewport = wp_viewporter_get_viewport(platform.viewporter(), native->surface);
            if (native->fractionalScale == nullptr || native->viewport == nullptr)
                return Result::Failure;
            static constexpr wp_fractional_scale_v1_listener fractionalListener{
                .preferred_scale = NativeState::preferredScale,
            };
            wp_fractional_scale_v1_add_listener(native->fractionalScale, &fractionalListener, native.get());
        }

        if (isChildWindow())
        {
            const Result result = createSubsurface(*native);
            if (result != Result::Success)
                return result;
        }
        else
        {
            native->shellSurface = xdg_wm_base_get_xdg_surface(platform.shell(), native->surface);
            native->toplevel = native->shellSurface != nullptr ? xdg_surface_get_toplevel(native->shellSurface)
                                                               : nullptr;
            if (native->shellSurface == nullptr || native->toplevel == nullptr)
                return Result::Failure;

            if (captionMode() == internal::WaylandCaptionMode::Detached)
            {
                native->captionSurface = wl_compositor_create_surface(platform.compositor());
                if (native->captionSurface != nullptr && platform.viewporter() != nullptr)
                {
                    native->captionViewport = wp_viewporter_get_viewport(platform.viewporter(), native->captionSurface);
                    if (native->captionViewport == nullptr)
                        return Result::Failure;
                }
                native->captionSubsurface = native->captionSurface != nullptr
                                                ? wl_subcompositor_get_subsurface(platform.subcompositor(),
                                                                                  native->captionSurface,
                                                                                  native->surface)
                                                : nullptr;
                if (native->captionSurface == nullptr || native->captionSubsurface == nullptr)
                    return Result::Failure;
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

        fNativeState = std::move(native);
        platform.registerWindow(fNativeState->surface, *this);
        if (fNativeState->captionSurface != nullptr)
            platform.registerWindow(fNativeState->captionSurface, *this, internal::WaylandSurfaceRole::Caption);

        if (!isChildWindow() && platform.decorationManager() != nullptr)
        {
            fNativeState->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(platform.decorationManager(),
                                                                                          fNativeState->toplevel);
            if (fNativeState->decoration == nullptr)
            {
                destroy();
                return Result::Failure;
            }
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
            wl_surface_commit(fParentBackend->fNativeState->surface);
        }
        else
        {
            setTitle(fTitle);
            if (!fAppId.empty())
                setAppId(fAppId);
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
            fPlatform.unregisterWindow(native->captionSurface);
        fPlatform.unregisterWindow(native->surface);
        fVisible = false;
        native.reset();
        dispatchEvent(EventWindowDestroyed{});
    }

    void WindowBackendWayland::show()
    {
        if (fNativeState != nullptr && !fVisible)
        {
            fVisible = true;
            if (isChildWindow())
            {
                if (fNativeState->viewport == nullptr &&
                    wl_surface_get_version(fNativeState->surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
                    wl_surface_set_buffer_scale(fNativeState->surface,
                                                std::max(1, static_cast<int32_t>(std::lround(fNativeState->scale))));
                fNativeState->inputRect.reset();
                updateSubsurfacePosition();
                updateSubsurfaceInputRegion();
                wl_surface_commit(fParentBackend->fNativeState->surface);
            }
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
            const bool hostFramedChild = fNativeState->subsurface != nullptr && fPlatform.hasHostWindowFrame();
            // WSLg keeps the last host buffer visible after a null-buffer commit. Keep the borrowed wl_surface
            // stable, but replace its visible content and input with one transparent pixel until show().
            wl_buffer* transparent = hostFramedChild ? fNativeState->getTransparentBuffer() : nullptr;
            if (transparent != nullptr && fNativeState->viewport != nullptr)
                wp_viewport_set_destination(fNativeState->viewport, 1, 1);
            else if (transparent != nullptr &&
                     wl_surface_get_version(fNativeState->surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
                wl_surface_set_buffer_scale(fNativeState->surface, 1);
            wl_surface_attach(fNativeState->surface, transparent, 0, 0);
            if (transparent != nullptr)
                wl_surface_damage(fNativeState->surface, 0, 0, 1, 1);
            if (!hostFramedChild)
                wl_surface_commit(fNativeState->surface);
            if (fNativeState->captionSurface != nullptr)
            {
                wl_surface_attach(fNativeState->captionSurface, nullptr, 0, 0);
                wl_surface_commit(fNativeState->captionSurface);
            }
            if (hostFramedChild)
            {
                fNativeState->inputRect.reset();
                updateSubsurfaceInputRegion();
                wl_surface_commit(fParentBackend->fNativeState->surface);
            }
            if (!isChildWindow())
                fNativeState->configured = false;
        }
    }

    bool WindowBackendWayland::isConfigured() const
    {
        return fNativeState != nullptr && fNativeState->configured;
    }

    bool WindowBackendWayland::getVisible() const
    {
        return fVisible;
    }

    void WindowBackendWayland::setDisplayState(WindowShowState state)
    {
        if (state == WindowShowState::Maximized && fDisplayState == WindowShowState::Restored && !fFullScreen &&
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
            case WindowShowState::Minimized:
                xdg_toplevel_set_minimized(fNativeState->toplevel);
                break;
            case WindowShowState::Maximized:
                xdg_toplevel_set_maximized(fNativeState->toplevel);
                break;
            case WindowShowState::Restored:
                xdg_toplevel_unset_maximized(fNativeState->toplevel);
                break;
        }
    }

    void WindowBackendWayland::maximize()
    {
        // Send the complete desired state; a previous compositor configure can still be in flight.
        setFullScreenState(internal::FullScreenState::Windowed);
        setDisplayState(WindowShowState::Maximized);
    }

    WindowShowState WindowBackendWayland::getDisplayState() const
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
    Result WindowBackendWayland::setWindowIcon(const BitmapBuffer*)
    {
        return Result::NotSupported;
    }
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
            {
                const Size framebuffer = getFramebufferSize();
                std::ignore = dispatchEvent(
                    EventClientAreaSizeChanged{{{fSize.x, fSize.y}, {framebuffer.x, framebuffer.y}}});
            }
            updateChildInputRegions();
        }
    }

    Size WindowBackendWayland::getClientSize() const
    {
        return fSize;
    }
    Size WindowBackendWayland::getFramebufferSize() const
    {
        const double scale = fNativeState != nullptr ? fNativeState->scale : 1.0;
        const double width = std::ceil(fSize.x * scale);
        const double height = std::ceil(fSize.y * scale);
        if (width > std::numeric_limits<int32_t>::max() || height > std::numeric_limits<int32_t>::max())
            return {};
        return {static_cast<int32_t>(width), static_cast<int32_t>(height)};
    }
    void WindowBackendWayland::setPlacement(const internal::NativeWindowPlacement& placement)
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
        updateContentScale();
        if (fNativeState == nullptr)
            return;
        const WindowShowState displayState = maximized ? WindowShowState::Maximized : WindowShowState::Restored;
        const bool displayStateChanged = displayState != fDisplayState;
        const bool constrained = fullscreen || maximized;
        if (constrained && !fFullScreen && fDisplayState == WindowShowState::Restored &&
            !fRestoredClientSize.has_value())
        {
            fRestoredClientSize = fSize;
        }
        fDisplayState = displayState;

        fFullScreen = fullscreen;
        if (fullscreen)
            fFullScreenState = internal::FullScreenState::SingleScreen;
        else if (fFullScreenState != internal::FullScreenState::None)
            fFullScreenState = internal::FullScreenState::Windowed;
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
            std::ignore = dispatchEvent(EventShowStateChanged{displayState});
        if (fNativeState != nullptr)
        {
            const Size framebuffer = getFramebufferSize();
            std::ignore = dispatchEvent(
                EventClientAreaSizeChanged{{{fSize.x, fSize.y}, {framebuffer.x, framebuffer.y}}});
        }
        updateChildInputRegions();
    }

    void WindowBackendWayland::setFullScreenState(internal::FullScreenState state)
    {
        const bool fullscreen = state != internal::FullScreenState::None &&
                                state != internal::FullScreenState::Windowed;
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

    internal::FullScreenState WindowBackendWayland::getFullScreenState() const
    {
        return fFullScreenState;
    }

    bool WindowBackendWayland::isMouseInClientRect() const
    {
        return fMouseInside;
    }
    Point WindowBackendWayland::getMousePosition() const
    {
        return fMousePosition;
    }
    void WindowBackendWayland::setLockMouseToWindowMode(internal::LockMouseToWindowMode) {}
    Result WindowBackendWayland::setPointerLocked(bool locked)
    {
        if (fNativeState == nullptr)
            return Result::NotCreated;
        if (locked == fPointerLockRequested)
            return Result::Success;

        auto& platform = fPlatform;
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
    void WindowBackendWayland::setCursor(std::shared_ptr<internal::ICursorBackend> cursor)
    {
        if (auto* previous = dynamic_cast<CursorBackendWayland*>(fCursor.get()); previous != nullptr)
            previous->detach(*this);
        fCursor = std::move(cursor);
        if (auto* current = dynamic_cast<CursorBackendWayland*>(fCursor.get()); current != nullptr)
            current->attach(*this);
        if (fNativeState != nullptr && fNativeState->frameCursorShape.has_value())
            fPlatform.applyCursor(*this, *fNativeState->frameCursorShape, true);
        else
            applyClientCursor();
    }

    void WindowBackendWayland::setParent(internal::IWindowBackend* parent)
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
        auto& platform = fPlatform;
        if (!platform.isInitialized())
            return Result::PlatformNotInitialized;
        if (enable && !platform.supportsDragAndDrop())
            return Result::NotSupported;
        fDragAndDropEnabled = enable;
        return Result::Success;
    }

    Result WindowBackendWayland::presentBitmap(const BitmapBuffer& bitmap)
    {
        if (fNativeState == nullptr)
            return Result::NotCreated;
        if (!fVisible || !fNativeState->configured)
            return Result::InvalidState;
        const Size framebuffer = getFramebufferSize();
        if (framebuffer.x <= 0 || framebuffer.y <= 0 || framebuffer.x > std::numeric_limits<int32_t>::max() / 4)
            return Result::Failure;
        const auto layout = internal::validateBitmapBuffer(bitmap);
        const size_t tightPitch = static_cast<size_t>(framebuffer.x) * 4U;
        if (!layout.has_value() || bitmap.format != BitmapPixelFormat::Bgra8Premultiplied ||
            bitmap.rowOrder != BitmapRowOrder::TopDown || bitmap.width != static_cast<uint32_t>(framebuffer.x) ||
            bitmap.height != static_cast<uint32_t>(framebuffer.y) || layout->rowPitch != tightPitch)
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

            wl_shm_pool* pool = wl_shm_create_pool(fPlatform.sharedMemory(), fd, static_cast<int32_t>(bufferSize));
            wl_buffer* buffer = pool != nullptr ? wl_shm_pool_create_buffer(pool, 0, framebuffer.x, framebuffer.y,
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
            wl_surface_damage_buffer(fNativeState->surface, 0, 0, framebuffer.x, framebuffer.y);
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
        return reinterpret_cast<Handle>(surface());
    }

    void* WindowBackendWayland::surface() const
    {
        return fNativeState != nullptr ? fNativeState->surface : nullptr;
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
                        if (dispatchEvent(EventCloseRequested{}) == EventResponse::Unhandled)
                            std::ignore = owner().Destroy();
                        break;
                    case internal::WaylandFrameAction::Move:
                    {
                        if (isCaptionDoubleClick(time, position))
                        {
                            setDisplayState(fDisplayState == WindowShowState::Maximized ? WindowShowState::Restored
                                                                                        : WindowShowState::Maximized);
                        }
                        else if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
                        {
                            auto& platform = fPlatform;
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
        fAppId = appId;
        if (fNativeState != nullptr && fNativeState->toplevel != nullptr)
            xdg_toplevel_set_app_id(fNativeState->toplevel, fAppId.c_str());
    }

    wl_display* WindowBackendWayland::display() const
    {
        return fPlatform.display();
    }

    double WindowBackendWayland::contentScale() const
    {
        return fNativeState != nullptr ? fNativeState->scale : 1.0;
    }

    WindowBackendWayland* WindowBackendWayland::dragDropTarget()
    {
        WindowBackendWayland* window = this;
        while (window != nullptr && !window->fDragAndDropEnabled)
            window = window->fParentBackend;
        return window;
    }

    void WindowBackendWayland::applyCursor(CursorShape shape, bool visible)
    {
        if (fNativeState == nullptr || !fNativeState->frameCursorShape.has_value())
            fPlatform.applyCursor(*this, shape, visible);
    }

    void WindowBackendWayland::applyClientCursor()
    {
        if (auto* waylandCursor = dynamic_cast<CursorBackendWayland*>(fCursor.get()); waylandCursor != nullptr)
            waylandCursor->apply();
        else
            fPlatform.applyCursor(*this, CursorShape::Arrow, true);
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
                fPlatform.applyCursor(*this, *shape, true);
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

        auto& platform = fPlatform;
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
        auto& platform = fPlatform;
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
        return internal::waylandCaptionMode(isChildWindow(), fPlatform.hasHostWindowFrame(), decorationMode(),
                                            captionStyle);
    }

    internal::WaylandDecorationMode WindowBackendWayland::decorationMode() const
    {
        if (fNativeState != nullptr && fNativeState->decoration != nullptr)
            return fNativeState->decorationMode;
        return fPlatform.decorationManager() == nullptr ? internal::WaylandDecorationMode::None
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
        return fParentBackend != nullptr;
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
        const Rect inputRect = fVisible ? internal::waylandChildInputRect(fPosition, fSize, parentSize,
                                                                          preserveParentResizeFrame)
                                        : Rect{};
        const Point topLeft = inputRect.GetCorner(LLUtils::TopLeft);
        const Point bottomRight = inputRect.GetCorner(LLUtils::BottomRight);
        if (fNativeState->inputRect.has_value() && fNativeState->inputRect->GetCorner(LLUtils::TopLeft) == topLeft &&
            fNativeState->inputRect->GetCorner(LLUtils::BottomRight) == bottomRight)
        {
            return;
        }

        wl_region* region = wl_compositor_create_region(fPlatform.compositor());
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
        if (fNativeState == nullptr || fSize.x <= 0 || fSize.y <= 0)
            return;

        if (fSize.y > std::numeric_limits<int32_t>::max() - internal::waylandCaptionHeight)
            return;
        const int32_t contentCaptionHeight = showsClientSideDecorations() ? internal::waylandCaptionHeight : 0;
        if (fNativeState->viewport != nullptr)
            wp_viewport_set_destination(fNativeState->viewport, fSize.x, fSize.y + contentCaptionHeight);
        if (fNativeState->captionViewport != nullptr)
            wp_viewport_set_destination(fNativeState->captionViewport, fSize.x, internal::waylandCaptionHeight);
        if (fNativeState->shellSurface != nullptr)
        {
            const bool detachedCaptionVisible = fVisible && showsDetachedCaption();
            const int32_t captionHeight = detachedCaptionVisible ? internal::waylandCaptionHeight : contentCaptionHeight;
            xdg_surface_set_window_geometry(fNativeState->shellSurface, 0,
                                            detachedCaptionVisible ? -internal::waylandCaptionHeight : 0, fSize.x,
                                            fSize.y + captionHeight);
        }
    }

    void WindowBackendWayland::handleOutputChange(wl_output* output, bool removed)
    {
        if (fNativeState == nullptr || std::ranges::find(fNativeState->outputs, output) == fNativeState->outputs.end())
            return;
        if (removed)
            std::erase(fNativeState->outputs, output);
        updateContentScale();
    }

    void WindowBackendWayland::updateContentScale()
    {
        if (fNativeState == nullptr || fNativeState->fractionalScale != nullptr ||
            wl_surface_get_version(fNativeState->surface) < WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
            return;
        int32_t scale = fNativeState->preferredBufferScaleValue.value_or(1);
        if (!fNativeState->preferredBufferScaleValue.has_value())
        {
            for (wl_output* output : fNativeState->outputs)
                scale = std::max(scale, fPlatform.outputScale(output));
        }
        setContentScale(static_cast<double>(scale));
    }

    void WindowBackendWayland::setContentScale(double scale)
    {
        if (fNativeState == nullptr || !std::isfinite(scale) || scale <= 0.0 || scale == fNativeState->scale)
            return;
        fNativeState->scale = scale;
        if (wl_surface_get_version(fNativeState->surface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
        {
            const int32_t bufferScale = fNativeState->viewport != nullptr ? 1 :
                                           std::max(1, static_cast<int32_t>(std::lround(scale)));
            wl_surface_set_buffer_scale(fNativeState->surface, bufferScale);
        }
        if (fNativeState->captionSurface != nullptr &&
            wl_surface_get_version(fNativeState->captionSurface) >= WL_SURFACE_SET_BUFFER_SCALE_SINCE_VERSION)
        {
            const int32_t bufferScale = fNativeState->captionViewport != nullptr ? 1 :
                                           std::max(1, static_cast<int32_t>(std::lround(scale)));
            wl_surface_set_buffer_scale(fNativeState->captionSurface, bufferScale);
        }
        updateWindowGeometry();
        if (fPointerLockActive)
            fPlatform.applyCursor(*this, CursorShape::Arrow, false);
        else if (fNativeState->frameCursorShape.has_value())
            fPlatform.applyCursor(*this, *fNativeState->frameCursorShape, true);
        else
            applyClientCursor();
        const Size framebuffer = getFramebufferSize();
        std::ignore = dispatchEvent(EventClientAreaSizeChanged{{{fSize.x, fSize.y}, {framebuffer.x, framebuffer.y}}});
        if (fNativeState != nullptr && fVisible && fNativeState->configured)
        {
            paintBackground();
            paintCaption();
        }
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
        const double scale = fNativeState->scale;
        const double scaledWidth = std::ceil(fSize.x * scale);
        const double scaledHeight = std::ceil((static_cast<double>(fSize.y) +
                                               (showsClientSideDecorations() ? internal::waylandCaptionHeight : 0)) * scale);
        if (scaledWidth > std::numeric_limits<int32_t>::max() || scaledHeight > std::numeric_limits<int32_t>::max())
            return;
        const size_t width = static_cast<size_t>(scaledWidth);
        const int32_t bufferHeight = static_cast<int32_t>(scaledHeight);
        const size_t height = static_cast<size_t>(bufferHeight);
        if (width > std::numeric_limits<size_t>::max() / bytesPerPixel / height)
        {
            return;
        }
        const size_t stride = width * bytesPerPixel;
        const size_t bufferSize = stride * height;
        if (bufferSize > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
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

        wl_shm_pool* pool = wl_shm_create_pool(fPlatform.sharedMemory(), fd, static_cast<int32_t>(bufferSize));
        wl_buffer* buffer = pool != nullptr
                                ? wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(width), bufferHeight,
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
            const int32_t captionHeight = static_cast<int32_t>(std::lround(internal::waylandCaptionHeight * scale));
            std::fill_n(pixels, width * captionHeight, captionColor);
            if ((std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::CloseButton)) != 0)
            {
                const int32_t closeLeft = static_cast<int32_t>(
                    std::lround(std::max(fSize.x - internal::waylandCloseButtonWidth, 0) * scale));
                for (int32_t y = 0; y < captionHeight; ++y)
                    std::fill_n(pixels + static_cast<size_t>(y) * width + closeLeft,
                                static_cast<int32_t>(width) - closeLeft, closeColor);
                const int32_t centerX = closeLeft + (static_cast<int32_t>(width) - closeLeft) / 2;
                const int32_t centerY = captionHeight / 2;
                const int32_t glyphRadius = std::max(1, static_cast<int32_t>(std::lround(5.0 * scale)));
                for (int32_t offset = -glyphRadius; offset <= glyphRadius; ++offset)
                {
                    const int32_t y = centerY + offset;
                    if (y >= 0 && y < captionHeight)
                    {
                        if (centerX + offset >= 0 && centerX + offset < static_cast<int32_t>(width))
                            pixels[static_cast<size_t>(y) * width + centerX + offset] = glyphColor;
                        if (centerX - offset >= 0 && centerX - offset < static_cast<int32_t>(width))
                            pixels[static_cast<size_t>(y) * width + centerX - offset] = glyphColor;
                    }
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
            wl_surface_damage_buffer(fNativeState->surface, 0, 0, static_cast<int32_t>(width), bufferHeight);
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
        const double scale = fNativeState->scale;
        const double scaledWidth = std::ceil(fSize.x * scale);
        const double scaledHeight = std::ceil(internal::waylandCaptionHeight * scale);
        if (scaledWidth > std::numeric_limits<int32_t>::max() || scaledHeight > std::numeric_limits<int32_t>::max() ||
            scaledWidth * bytesPerPixel * scaledHeight > std::numeric_limits<int32_t>::max())
            return;
        const size_t width = static_cast<size_t>(scaledWidth);
        const int32_t captionHeight = static_cast<int32_t>(scaledHeight);
        const size_t stride = width * bytesPerPixel;
        const size_t bufferSize = stride * captionHeight;
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

        wl_shm_pool* pool = wl_shm_create_pool(fPlatform.sharedMemory(), fd, static_cast<int32_t>(bufferSize));
        wl_buffer* buffer = pool != nullptr
                                ? wl_shm_pool_create_buffer(pool, 0, static_cast<int32_t>(width), captionHeight,
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
        std::fill_n(pixels, width * captionHeight, captionColor);
        const bool closeButton = (std::to_underlying(fWindowStyles) & std::to_underlying(WindowStyle::CloseButton)) !=
                                 0;
        const int32_t titleRight = static_cast<int32_t>(
            std::lround((closeButton ? std::max(fSize.x - internal::waylandCloseButtonWidth, 0) : fSize.x) * scale));
        internal::renderCaptionTitle({pixels, width * captionHeight}, static_cast<int32_t>(width), captionHeight,
                                     fTitle, titleRight, scale);
        if (closeButton)
        {
            const int32_t closeLeft = static_cast<int32_t>(
                std::lround(std::max(fSize.x - internal::waylandCloseButtonWidth, 0) * scale));
            for (int32_t y = 0; y < captionHeight; ++y)
                std::fill_n(pixels + static_cast<size_t>(y) * width + closeLeft,
                            static_cast<int32_t>(width) - closeLeft, closeColor);
            const int32_t centerX = closeLeft + (static_cast<int32_t>(width) - closeLeft) / 2;
            const int32_t centerY = captionHeight / 2;
            const int32_t glyphRadius = std::max(1, static_cast<int32_t>(std::lround(5.0 * scale)));
            for (int32_t offset = -glyphRadius; offset <= glyphRadius; ++offset)
            {
                const int32_t y = centerY + offset;
                if (y >= 0 && y < captionHeight)
                {
                    if (centerX + offset >= 0 && centerX + offset < static_cast<int32_t>(width))
                        pixels[static_cast<size_t>(y) * width + centerX + offset] = glyphColor;
                    if (centerX - offset >= 0 && centerX - offset < static_cast<int32_t>(width))
                        pixels[static_cast<size_t>(y) * width + centerX - offset] = glyphColor;
                }
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
            wl_surface_damage_buffer(fNativeState->captionSurface, 0, 0, static_cast<int32_t>(width), captionHeight);
        else
            wl_surface_damage(fNativeState->captionSurface, 0, 0, fSize.x, internal::waylandCaptionHeight);
        wl_surface_commit(fNativeState->captionSurface);
        fNativeState->captionWidth = fSize.x;
        fNativeState->captionBuffers.push_back(std::move(captionBuffer));
    }
}  // namespace LWS

#endif
