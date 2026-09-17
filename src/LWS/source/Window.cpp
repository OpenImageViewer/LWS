#include <LWS/Window.hpp>

#include "internal/ListenerState.hpp"
#include "internal/PlatformBackend.hpp"
#include "internal/WindowBackendAccess.hpp"

#include <LWS/source/internal/Backends.hpp>

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace
{
    bool ValidSizeLimits(const LWS::ClientSizeLimits& limits)
    {
        const auto& [minimum, maximum] = limits;
        return minimum.x >= 0 && minimum.y >= 0 && maximum.x >= 0 && maximum.y >= 0 &&
               (maximum.x == 0 || minimum.x <= maximum.x) && (maximum.y == 0 || minimum.y <= maximum.y);
    }
}  // namespace

namespace LWS
{
    class Window::Impl final
    {
      public:

        enum class State
        {
            PreCreate,
            Creating,
            Created,
            Destroying,
            Destroyed
        };

        std::unique_ptr<internal::IWindowBackend> backend;
        std::shared_ptr<internal::ListenerState> listeners;
        std::shared_ptr<internal::ICursorBackend> cursorBackend;
        std::optional<Cursor> cursor;
        std::optional<WindowIcon> icon;
        Window* parent{};
        std::vector<Window*> children;
        State state{State::PreCreate};
        WindowConfig config;
        ClientAreaMetrics clientArea{{800, 600}, std::nullopt};
        bool configured{};
        bool cursorVisible{true};
        bool notifyingDestroy{};
        bool nativeTeardown{};
    };

    bool internal::WindowBackendAccess::HasNativeHandle(const Window& window)
    {
        window.platform_.AssertCurrentThread();
        return window.platform_.IsUsable() && !window.impl_->nativeTeardown &&
               (window.impl_->state == Window::Impl::State::Created ||
                window.impl_->state == Window::Impl::State::Destroying);
    }

    bool internal::WindowBackendAccess::CanConfigure(const Window& window)
    {
        window.platform_.AssertCurrentThread();
        return window.platform_.IsUsable() && window.impl_->state != Window::Impl::State::Destroying &&
               window.impl_->state != Window::Impl::State::Destroyed;
    }

    internal::IWindowBackend* internal::WindowBackendAccess::Get(Window& window)
    {
        return window.impl_->backend.get();
    }

    const internal::IWindowBackend* internal::WindowBackendAccess::Get(const Window& window)
    {
        return window.impl_->backend.get();
    }

    EventResponse internal::WindowBackendAccess::Dispatch(Window& window, const AnyEvent& event)
    {
        return window.DispatchEvent(event);
    }

#ifdef LWS_PLATFORM_WIN32
    std::expected<EventConnection, Result> internal::WindowBackendAccess::ListenPlatform(
        Window& window, Win32::PlatformCallback callback)
    {
        window.platform_.AssertCurrentThread();
        if (!callback)
            return std::unexpected(Result::InvalidArgument);
        if (!CanConfigure(window) || window.impl_->listeners->IsClosed())
            return std::unexpected(Result::InvalidState);
        if (window.platform_.GetBackendId() != BackendId::Win32)
            return std::unexpected(Result::NotSupported);
        const uint64_t id = window.impl_->listeners->AddPlatform(std::move(callback));
        return EventConnection(window.impl_->listeners, id, std::this_thread::get_id());
    }

    bool internal::WindowBackendAccess::DispatchPlatform(Window& window, const Win32::PlatformEvent& event,
                                                         LRESULT& result)
    {
        const DispatchScope ownerDispatch(window);
        const PlatformContextAccess::DispatchScope contextDispatch(window.platform_);
        if (!window.IsCreated())
            return false;
        return window.impl_->listeners->DispatchPlatform(event, result);
    }
#endif

    EventResponse internal::IWindowBackend::dispatchEvent(const AnyEvent& event)
    {
        return internal::WindowBackendAccess::Dispatch(owner(), event);
    }

    Window::Window(PlatformContext& platform) : platform_(platform), impl_(std::make_unique<Impl>())
    {
        platform_.AssertCurrentThread();
        assert(platform_.IsUsable());
        impl_->listeners = std::make_shared<internal::ListenerState>(platform_);
        impl_->backend = internal::PlatformContextAccess::CreateWindowBackend(platform_, *this);
        assert(impl_->backend != nullptr);
        platform_.RegisterWindow();
    }

    Window::~Window()
    {
        platform_.AssertCurrentThread();
#ifndef NDEBUG
        assert(dispatchDepth_ == 0 && "Cannot delete a Window involved in active dispatch");
#endif
        assert(CanDestroy() && "Cannot delete a Window during conflicting family cleanup");
        std::ignore = Destroy();
        platform_.UnregisterWindow();
    }

    Result Window::Create(const WindowConfig& config)
    {
        platform_.AssertCurrentThread();
        if (!platform_.IsUsable() || impl_->state != Impl::State::PreCreate)
            return Result::InvalidState;
        if (config.clientSize.x <= 0 || config.clientSize.y <= 0 || !ValidSizeLimits(config.clientSizeLimits) ||
            (config.showState != WindowShowState::Restored && config.showState != WindowShowState::Maximized &&
             config.showState != WindowShowState::Minimized))
        {
            return Result::InvalidArgument;
        }
        if (config.parent != nullptr)
        {
            if (config.parent == this || &config.parent->platform_ != &platform_)
                return Result::InvalidArgument;
            if (!config.parent->IsCreated())
                return Result::InvalidState;
        }
        if ((config.alwaysOnTop && !platform_.Supports(PlatformFeature::AlwaysOnTop).value_or(false)) ||
            (platform_.GetBackendId() == BackendId::Wayland && config.showState == WindowShowState::Minimized))
            return Result::NotSupported;

        impl_->state = Impl::State::Creating;
        impl_->backend->setParent(config.parent != nullptr ? config.parent->impl_->backend.get() : nullptr);
        const internal::NativeWindowConfig nativeConfig{
            .position = config.position.value_or(Point{100, 100}),
            .size = static_cast<Size>(config.clientSize),
            .title = config.title,
            .styles = config.styles.get(),
            .displayState = config.showState,
            .visible = config.visible,
            .eraseBackground = config.eraseBackground,
            .alwaysOnTop = config.alwaysOnTop,
            .transparent = config.transparent,
            .minSize = static_cast<Size>(config.clientSizeLimits.minimum),
            .maxSize = static_cast<Size>(config.clientSizeLimits.maximum),
        };
        impl_->backend->setBackgroundColor(config.backgroundColor);
        Result result = Result::Success;
        if (impl_->icon.has_value())
        {
            const BitmapBuffer icon = impl_->icon->GetBuffer();
            result = impl_->backend->setWindowIcon(&icon);
        }
        if (result == Result::Success && config.dragAndDropEnabled)
            result = impl_->backend->enableDragAndDrop(true);
        if (result == Result::Success && impl_->cursor.has_value())
            result = SetMouseCursor(*impl_->cursor);
        if (result == Result::Success)
            result = impl_->backend->create(nativeConfig);

        if (result != Result::Success)
        {
            impl_->backend->destroy();
            impl_->backend->setParent(nullptr);
            impl_->state = Impl::State::PreCreate;
            return result;
        }

        impl_->config = config;
        impl_->parent = config.parent;
        if (impl_->parent != nullptr)
            impl_->parent->impl_->children.push_back(this);
        const Size clientSize = impl_->backend->getClientSize();
        const Size framebufferSize = impl_->backend->getFramebufferSize();
        impl_->clientArea = {{clientSize.x, clientSize.y}, PixelSize{framebufferSize.x, framebufferSize.y}};
        impl_->configured = impl_->backend->isConfigured();
        impl_->state = Impl::State::Created;
        return Result::Success;
    }

    bool Window::CanDestroy() const
    {
        if (impl_->state == Impl::State::Creating || impl_->state == Impl::State::Destroying)
            return false;
        for (const Window* parent = impl_->parent; parent != nullptr; parent = parent->impl_->parent)
            if (parent->impl_->notifyingDestroy)
                return false;
        return std::ranges::all_of(impl_->children, [](const Window* child) { return child->CanDestroy(); });
    }

    Result Window::Destroy()
    {
        platform_.AssertCurrentThread();
        if (impl_->state == Impl::State::PreCreate || impl_->state == Impl::State::Destroyed)
            return Result::Success;
        if (!CanDestroy())
            return Result::InvalidState;

        const internal::WindowBackendAccess::DispatchScope ownerDispatch(*this);
        const internal::PlatformContextAccess::DispatchScope contextDispatch(platform_);
        impl_->state = Impl::State::Destroying;
        // Parent cleanup precedes child teardown. Requests that would destroy a dependency while a parent's
        // cleanup listeners are still running are rejected, rather than requiring deferred destruction machinery.
        impl_->notifyingDestroy = true;
        std::ignore = DispatchEvent(EventWindowDestroying{platform_.IsUsable()});
        impl_->notifyingDestroy = false;
        const auto children = impl_->children;
        for (Window* child : children)
        {
            if (std::ranges::find(impl_->children, child) != impl_->children.end())
                std::ignore = child->Destroy();
        }
        impl_->nativeTeardown = true;
        impl_->backend->destroy();
        if (impl_->state == Impl::State::Destroying)
            std::ignore = DispatchEvent(EventWindowDestroyed{});
        return Result::Success;
    }

    bool Window::IsCreated() const
    {
        platform_.AssertCurrentThread();
        return platform_.IsUsable() && impl_->state == Impl::State::Created;
    }

    PlatformContext& Window::GetPlatformContext()
    {
        platform_.AssertCurrentThread();
        return platform_;
    }
    const PlatformContext& Window::GetPlatformContext() const
    {
        platform_.AssertCurrentThread();
        return platform_;
    }
    Result Window::SetTitle(const string_type& title)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        impl_->backend->setTitle(title);
        impl_->config.title = title;
        return Result::Success;
    }

    string_type Window::GetTitle() const
    {
        platform_.AssertCurrentThread();
        return IsCreated() ? impl_->backend->getTitle() : impl_->config.title;
    }

    Result Window::SetVisible(bool visible)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (visible == impl_->backend->getVisible())
            return Result::Success;
        if (platform_.GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            impl_->configured = false;
        visible ? impl_->backend->show() : impl_->backend->hide();
        impl_->config.visible = visible;
        return Result::Success;
    }

    bool Window::IsVisible() const
    {
        platform_.AssertCurrentThread();
        return IsCreated() ? impl_->backend->getVisible() : impl_->config.visible;
    }

    ClientAreaMetrics Window::GetClientAreaMetrics() const
    {
        platform_.AssertCurrentThread();
        ClientAreaMetrics metrics = impl_->clientArea;
        if (!impl_->configured || !internal::WindowBackendAccess::HasNativeHandle(*this))
        {
            const Size logical = IsCreated() ? impl_->backend->getClientSize()
                                             : static_cast<Size>(impl_->config.clientSize);
            metrics = {{logical.x, logical.y}, std::nullopt};
        }
        return metrics;
    }

    Result Window::RequestPlacement(const WindowPlacementRequest& request)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if ((!request.position.has_value() && !request.clientSize.has_value()) ||
            (request.clientSize.has_value() && (request.clientSize->x <= 0 || request.clientSize->y <= 0)))
            return Result::InvalidArgument;
        if (request.position.has_value() && platform_.GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return Result::NotSupported;

        if (request.position.has_value() && request.clientSize.has_value())
        {
            impl_->backend->setPlacement({*request.position, static_cast<Size>(*request.clientSize)});
        }
        else if (request.position.has_value())
        {
            impl_->backend->setPosition(*request.position);
        }
        else
        {
            impl_->backend->setSize(static_cast<Size>(*request.clientSize));
        }
        if (request.position.has_value())
            impl_->config.position = request.position;
        if (request.clientSize.has_value())
            impl_->config.clientSize = *request.clientSize;
        return Result::Success;
    }

    WindowPlacement Window::GetPlacement() const
    {
        platform_.AssertCurrentThread();
        std::optional<Point> position;
        if (platform_.GetBackendId() != BackendId::Wayland || impl_->parent != nullptr)
            position = IsCreated() ? std::optional(impl_->backend->getPosition()) : impl_->config.position;
        return {position, GetClientAreaMetrics().logical};
    }

    Result Window::SetClientSizeLimits(ClientSizeLimits limits)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (!ValidSizeLimits(limits))
            return Result::InvalidArgument;
        impl_->backend->setMinMaxSize(static_cast<Size>(limits.minimum), static_cast<Size>(limits.maximum));
        impl_->config.clientSizeLimits = limits;
        return Result::Success;
    }

    ClientSizeLimits Window::GetClientSizeLimits() const
    {
        platform_.AssertCurrentThread();
        ClientSizeLimits limits = impl_->config.clientSizeLimits;
        if (IsCreated())
        {
            const Size minimum = impl_->backend->getMinSize();
            const Size maximum = impl_->backend->getMaxSize();
            limits = {{minimum.x, minimum.y}, {maximum.x, maximum.y}};
        }
        return limits;
    }

    Result Window::Center(CenterTarget target)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (platform_.GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return Result::NotSupported;

        Rect area;
        if (target != CenterTarget::Parent && target != CenterTarget::CurrentMonitor &&
            target != CenterTarget::PrimaryMonitor)
            return Result::InvalidArgument;
        if (target == CenterTarget::Parent)
        {
            if (impl_->parent == nullptr)
                return Result::InvalidState;
            const LogicalSize parentSize = impl_->parent->GetClientAreaMetrics().logical;
            area = {{0, 0}, {parentSize.x, parentSize.y}};
        }
        else
        {
            const auto monitor = target == CenterTarget::CurrentMonitor
                                     ? platform_.GetMonitorInfo(impl_->backend->getCurrentMonitorHandle())
                                     : platform_.GetPrimaryMonitor();
            if (!monitor.has_value())
                return monitor.error();
            area = monitor->workRect;
            if (platform_.GetBackendId() == BackendId::Win32)
            {
                const auto scalePoint = [](Point point, ContentScale scale)
                {
                    return Point{static_cast<int32_t>(std::lround(point.x / scale.x)),
                                 static_cast<int32_t>(std::lround(point.y / scale.y))};
                };
                area = {scalePoint(area.LeftTop(), monitor->contentScale),
                        scalePoint(area.RightBottom(), monitor->contentScale)};
            }
        }
        const LogicalSize size = GetClientAreaMetrics().logical;
        const Point origin = area.LeftTop();
        return RequestPlacement(
            {.position = Point{origin.x + (area.GetWidth() - size.x) / 2, origin.y + (area.GetHeight() - size.y) / 2}});
    }

    Result Window::SetWindowMode(WindowMode mode)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (mode != WindowMode::Windowed && mode != WindowMode::Fullscreen && mode != WindowMode::FullscreenAllMonitors)
            return Result::InvalidArgument;
        if (mode == WindowMode::FullscreenAllMonitors && platform_.GetBackendId() == BackendId::Wayland)
            return Result::NotSupported;
        const auto nativeMode = mode == WindowMode::Windowed
                                    ? internal::FullScreenState::Windowed
                                    : (mode == WindowMode::Fullscreen ? internal::FullScreenState::SingleScreen
                                                                      : internal::FullScreenState::MultiScreen);
        impl_->backend->setFullScreenState(nativeMode);
        return Result::Success;
    }

    WindowMode Window::GetWindowMode() const
    {
        platform_.AssertCurrentThread();
        const auto mode = impl_->backend->getFullScreenState();
        if (mode == internal::FullScreenState::MultiScreen)
            return WindowMode::FullscreenAllMonitors;
        return mode == internal::FullScreenState::SingleScreen ? WindowMode::Fullscreen : WindowMode::Windowed;
    }

    Result Window::RequestShowState(WindowShowState state)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (state != WindowShowState::Restored && state != WindowShowState::Maximized &&
            state != WindowShowState::Minimized)
            return Result::InvalidArgument;
        if (state == WindowShowState::Minimized && platform_.GetBackendId() == BackendId::Wayland)
            return Result::NotSupported;
        impl_->backend->setDisplayState(state);
        impl_->config.showState = state;
        return Result::Success;
    }

    Result Window::RequestMaximize()
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (impl_->parent != nullptr)
            return Result::NotSupported;
        impl_->backend->maximize();
        impl_->config.showState = WindowShowState::Maximized;
        return Result::Success;
    }

    WindowShowState Window::GetShowState() const
    {
        return IsCreated() ? impl_->backend->getDisplayState() : impl_->config.showState;
    }

    bool Window::IsConfigured() const
    {
        platform_.AssertCurrentThread();
        return IsCreated() && impl_->configured;
    }

    Result Window::RequestActivation()
    {
        if (!IsCreated())
            return Result::InvalidState;
        if (platform_.GetBackendId() != BackendId::Win32 || impl_->parent != nullptr)
            return Result::NotSupported;
        impl_->backend->setForeground();
        return Result::Success;
    }

    bool Window::HasKeyboardFocus() const
    {
        return IsCreated() && impl_->backend->isInFocus();
    }

    Result Window::SetWindowStyles(WindowStyleFlags styles)
    {
        if (!IsCreated())
            return Result::InvalidState;
        if (impl_->backend->getWindowStyles() == styles.get())
            return Result::Success;
        impl_->backend->setWindowStyles(styles.get());
        impl_->config.styles = styles;
        return Result::Success;
    }

    WindowStyleFlags Window::GetWindowStyles() const
    {
        return IsCreated() ? WindowStyleFlags(impl_->backend->getWindowStyles()) : impl_->config.styles;
    }

    Result Window::SetAlwaysOnTop(bool onTop)
    {
        if (!IsCreated())
            return Result::InvalidState;
        const auto supported = platform_.Supports(PlatformFeature::AlwaysOnTop);
        if (!supported.has_value())
            return supported.error();
        if (!*supported)
            return Result::NotSupported;
        impl_->backend->setAlwaysOnTop(onTop);
        impl_->config.alwaysOnTop = onTop;
        return Result::Success;
    }

    bool Window::IsAlwaysOnTop() const
    {
        return IsCreated() ? impl_->backend->getAlwaysOnTop() : impl_->config.alwaysOnTop;
    }

    Result Window::SetTransparent(bool transparent)
    {
        if (!IsCreated())
            return Result::InvalidState;
        impl_->backend->setTransparent(transparent);
        impl_->config.transparent = transparent;
        return Result::Success;
    }

    bool Window::IsTransparent() const
    {
        return IsCreated() ? impl_->backend->getTransparent() : impl_->config.transparent;
    }

    Result Window::SetBackgroundColor(LLUtils::Color color)
    {
        if (!IsCreated())
            return Result::InvalidState;
        impl_->backend->setBackgroundColor(color);
        impl_->config.backgroundColor = color;
        return Result::Success;
    }

    Result Window::SetEraseBackground(bool erase)
    {
        if (!IsCreated())
            return Result::InvalidState;
        impl_->backend->setEraseBackground(erase);
        impl_->config.eraseBackground = erase;
        return Result::Success;
    }

    bool Window::IsBackgroundErasureEnabled() const
    {
        return IsCreated() ? impl_->backend->getEraseBackground() : impl_->config.eraseBackground;
    }

    Result Window::SetDragAndDropEnabled(bool enable)
    {
        if (!IsCreated())
            return Result::InvalidState;
        const Result result = impl_->backend->enableDragAndDrop(enable);
        if (result == Result::Success)
            impl_->config.dragAndDropEnabled = enable;
        return result;
    }

    bool Window::IsMouseInClientRect() const
    {
        return IsCreated() && impl_->backend->isMouseInClientRect();
    }
    Point Window::GetMousePosition() const
    {
        return IsCreated() ? impl_->backend->getMousePosition() : Point{};
    }

    Result Window::SetPointerLocked(bool locked)
    {
        return IsCreated() ? impl_->backend->setPointerLocked(locked) : Result::InvalidState;
    }

    Result Window::BeginWindowDrag(WindowDragOperation operation)
    {
        if (!IsCreated() || impl_->parent != nullptr)
            return Result::InvalidState;
        if (operation != WindowDragOperation::Move && operation != WindowDragOperation::ResizeNearest)
            return Result::InvalidArgument;
        if (platform_.GetBackendId() != BackendId::Win32)
            return Result::NotSupported;
        impl_->backend->setLockMouseToWindowMode(operation == WindowDragOperation::Move
                                                     ? internal::LockMouseToWindowMode::LockMove
                                                     : internal::LockMouseToWindowMode::LockResize);
        return Result::Success;
    }

    Result Window::SetMouseCursor(Cursor cursor)
    {
        platform_.AssertCurrentThread();
        if (cursor.resource_ == nullptr)
            return Result::InvalidArgument;
        if (!internal::WindowBackendAccess::CanConfigure(*this))
            return Result::InvalidState;
        const bool custom = cursor.IsCustom();
        if (custom && platform_.GetBackendId() == BackendId::Wayland)
            return Result::NotSupported;
        if (impl_->state == Impl::State::PreCreate)
        {
            impl_->cursor = std::move(cursor);
            return Result::Success;
        }
        if (impl_->state == Impl::State::Created && custom && impl_->cursorBackend != nullptr &&
            impl_->cursor.has_value() && impl_->cursor->resource_ == cursor.resource_)
        {
            // Custom native cursors depend only on immutable pixels/hotspot, not window DPI. Keep applying
            // visibility/the current handle: another window may have selected a different cursor meanwhile.
            impl_->cursorBackend->setVisible(impl_->cursorVisible);
            return Result::Success;
        }
        if (impl_->cursorBackend == nullptr)
        {
            auto nativeCursor = internal::createDefaultCursorBackend();
            impl_->cursorBackend = std::shared_ptr<internal::ICursorBackend>(std::move(nativeCursor));
            impl_->backend->setCursor(impl_->cursorBackend);
        }
        const Result result = custom ? impl_->cursorBackend->setCustomCursor(cursor.BitmapData(), cursor.Hotspot())
                                     : (impl_->cursorBackend->setCursorShape(cursor.Shape()), Result::Success);
        if (result == Result::Success)
        {
            impl_->cursor = std::move(cursor);
            impl_->cursorBackend->setVisible(impl_->cursorVisible);
        }
        return result;
    }

    Result Window::SetMouseCursorVisible(bool visible)
    {
        platform_.AssertCurrentThread();
        if (!internal::WindowBackendAccess::CanConfigure(*this))
            return Result::InvalidState;
        impl_->cursorVisible = visible;
        if (!impl_->cursor.has_value())
            return SetMouseCursor(Cursor::FromShape(CursorShape::Arrow));
        if (impl_->cursorBackend != nullptr)
            impl_->cursorBackend->setVisible(visible);
        return Result::Success;
    }

    Result Window::SetWindowIcon(std::optional<WindowIcon> icon)
    {
        platform_.AssertCurrentThread();
        if (icon.has_value() && icon->bitmap_ == nullptr)
            return Result::InvalidArgument;
        if (!internal::WindowBackendAccess::CanConfigure(*this))
            return Result::InvalidState;
        if (icon.has_value() && platform_.GetBackendId() != BackendId::Win32)
            return Result::NotSupported;
        Result result = Result::Success;
        if (impl_->state == Impl::State::PreCreate)
        {
            impl_->icon = std::move(icon);
        }
        else
        {
            if (icon.has_value())
            {
                const BitmapBuffer bitmap = icon->GetBuffer();
                const bool reuseResource = impl_->state == Impl::State::Created && impl_->icon.has_value() &&
                                           impl_->icon->bitmap_ == icon->bitmap_;
                result = impl_->backend->setWindowIcon(&bitmap, reuseResource);
            }
            else
            {
                result = impl_->backend->setWindowIcon(nullptr);
            }
            if (result == Result::Success)
                impl_->icon = std::move(icon);
        }
        return result;
    }

    Window* Window::GetParent() const
    {
        platform_.AssertCurrentThread();
        return impl_->parent;
    }

    std::expected<EventConnection, Result> Window::Listen(EventCallback callback)
    {
        return Listen(std::move(callback), false);
    }

    std::expected<EventConnection, Result> Window::Listen(EventCallback callback, bool beforeUserCallbacks)
    {
        platform_.AssertCurrentThread();
        if (!callback)
            return std::unexpected(Result::InvalidArgument);
        if (!internal::WindowBackendAccess::CanConfigure(*this) || impl_->listeners->IsClosed())
            return std::unexpected(Result::InvalidState);
        const uint64_t id = impl_->listeners->Add(std::move(callback), beforeUserCallbacks);
        return EventConnection(impl_->listeners, id, std::this_thread::get_id());
    }

    Result Window::PresentBitmap(const BitmapBuffer& bitmap)
    {
        if (!IsConfigured())
            return Result::InvalidState;
        if (PixelSize{static_cast<int32_t>(bitmap.width), static_cast<int32_t>(bitmap.height)} !=
            impl_->clientArea.pixels)
        {
            return Result::InvalidArgument;
        }
        return impl_->backend->presentBitmap(bitmap);
    }

    EventResponse Window::DispatchEvent(const AnyEvent& event)
    {
        if (impl_->state == Impl::State::Creating || impl_->state == Impl::State::PreCreate ||
            impl_->state == Impl::State::Destroyed)
        {
            return EventResponse::Unhandled;
        }

        const bool lifecycle = std::holds_alternative<EventWindowDestroying>(event) ||
                               std::holds_alternative<EventWindowDestroyed>(event);
        if (!lifecycle && !IsCreated())
            return EventResponse::Unhandled;

        if (const auto* sizeEvent = std::get_if<EventClientAreaSizeChanged>(&event); sizeEvent != nullptr)
        {
            if (impl_->configured && impl_->clientArea == sizeEvent->size)
                return EventResponse::Unhandled;
            impl_->clientArea = sizeEvent->size;
            impl_->config.clientSize = sizeEvent->size.logical;
            impl_->configured = true;
        }
        if (const auto* stateEvent = std::get_if<EventShowStateChanged>(&event); stateEvent != nullptr)
            impl_->config.showState = stateEvent->state;

        if (std::holds_alternative<EventWindowDestroyed>(event))
        {
            impl_->state = Impl::State::Destroying;
            impl_->nativeTeardown = true;
        }
        const internal::WindowBackendAccess::DispatchScope ownerDispatch(*this);
        const internal::PlatformContextAccess::DispatchScope contextDispatch(platform_);
        const EventResponse response = impl_->listeners->Dispatch(event);
        if (std::holds_alternative<EventWindowDestroyed>(event))
        {
            impl_->configured = false;
            impl_->state = Impl::State::Destroyed;
            impl_->listeners->Close();
            if (impl_->parent != nullptr)
            {
                std::erase(impl_->parent->impl_->children, this);
                impl_->parent = nullptr;
            }
            for (Window* child : impl_->children)
                child->impl_->parent = nullptr;
            impl_->children.clear();
            impl_->backend->setCursor(nullptr);
            impl_->cursorBackend.reset();
            impl_->cursor.reset();
            impl_->icon.reset();
        }
        return response;
    }
}  // namespace LWS
