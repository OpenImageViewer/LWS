#include <LWS/Window.hpp>

#include "internal/ListenerState.hpp"
#include "internal/PlatformBackend.hpp"
#include "internal/WindowBackendAccess.hpp"

#include <LWS/interfaces/backends.hpp>

#include <algorithm>
#include <cassert>
#include <utility>
#include <vector>

namespace
{
    bool ValidSizeLimits(LWS::LogicalSize minimum, LWS::LogicalSize maximum)
    {
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
        ClientAreaSize clientArea{{800, 600}, {800, 600}};
        bool configured{};
        bool cursorVisible{true};
    };

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
        if (window.impl_->listeners->IsClosed())
            return std::unexpected(Result::InvalidState);
        if (window.GetBackendId() != BackendId::Win32)
            return std::unexpected(Result::NotSupported);
        const uint64_t id = window.impl_->listeners->AddPlatform(std::move(callback));
        return EventConnection(window.impl_->listeners, id, std::this_thread::get_id());
    }

    bool internal::WindowBackendAccess::DispatchPlatform(Window& window, const Win32::PlatformEvent& event,
                                                         LRESULT& result)
    {
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
        std::ignore = Destroy();
        platform_.UnregisterWindow();
    }

    Result Window::Create(const WindowConfig& config)
    {
        platform_.AssertCurrentThread();
        if (impl_->state != Impl::State::PreCreate)
            return Result::InvalidState;
        if (config.clientSize.x <= 0 || config.clientSize.y <= 0 ||
            !ValidSizeLimits(config.minClientSize, config.maxClientSize))
        {
            return Result::InvalidArgument;
        }
        if (config.parent != nullptr &&
            (config.parent == this || !config.parent->IsCreated() || &config.parent->platform_ != &platform_))
        {
            return Result::InvalidArgument;
        }

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
            .minSize = static_cast<Size>(config.minClientSize),
            .maxSize = static_cast<Size>(config.maxClientSize),
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
        impl_->clientArea = {{clientSize.x, clientSize.y}, {framebufferSize.x, framebufferSize.y}};
        impl_->configured = impl_->backend->isConfigured();
        impl_->state = Impl::State::Created;
        return Result::Success;
    }

    Result Window::Destroy()
    {
        platform_.AssertCurrentThread();
        if (impl_->state == Impl::State::PreCreate || impl_->state == Impl::State::Destroyed)
            return Result::Success;
        if (impl_->state != Impl::State::Created)
            return Result::InvalidState;

        impl_->state = Impl::State::Destroying;
        const auto children = impl_->children;
        for (Window* child : children)
        {
            if (std::ranges::find(impl_->children, child) != impl_->children.end())
                std::ignore = child->Destroy();
        }
        impl_->backend->destroy();
        if (impl_->state == Impl::State::Destroying)
            std::ignore = DispatchEvent(EventWindowDestroyed{});
        return Result::Success;
    }

    bool Window::IsCreated() const
    {
        platform_.AssertCurrentThread();
        return impl_->state == Impl::State::Created;
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
    BackendId Window::GetBackendId() const
    {
        return *platform_.GetBackendId();
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
        if (GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            impl_->configured = false;
        visible ? impl_->backend->show() : impl_->backend->hide();
        impl_->config.visible = visible;
        return Result::Success;
    }

    bool Window::GetVisible() const
    {
        platform_.AssertCurrentThread();
        return IsCreated() ? impl_->backend->getVisible() : impl_->config.visible;
    }

    Result Window::SetPosition(Point position)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return Result::NotSupported;
        impl_->backend->setPosition(position);
        impl_->config.position = position;
        return Result::Success;
    }

    std::optional<Point> Window::GetPosition() const
    {
        platform_.AssertCurrentThread();
        if (GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return std::nullopt;
        return IsCreated() ? std::optional(impl_->backend->getPosition()) : impl_->config.position;
    }

    Result Window::RequestClientSize(LogicalSize size)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (size.x <= 0 || size.y <= 0)
            return Result::InvalidArgument;
        impl_->backend->setSize(static_cast<Size>(size));
        impl_->config.clientSize = size;
        return Result::Success;
    }

    LogicalSize Window::GetClientSize() const
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return impl_->config.clientSize;
        const Size size = impl_->backend->getClientSize();
        return {size.x, size.y};
    }

    std::expected<ClientAreaSize, Result> Window::GetClientAreaSize() const
    {
        platform_.AssertCurrentThread();
        if (!IsConfigured())
            return std::unexpected(Result::InvalidState);
        return impl_->clientArea;
    }

    Result Window::SetPlacement(const WindowPlacement& placement)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (placement.clientSize.x <= 0 || placement.clientSize.y <= 0)
            return Result::InvalidArgument;
        if (placement.position.has_value() && GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return Result::NotSupported;
        if (!placement.position.has_value())
            return RequestClientSize(placement.clientSize);

        impl_->backend->setPlacement(
            {*placement.position, static_cast<Size>(placement.clientSize), impl_->config.showState});
        impl_->config.position = placement.position;
        impl_->config.clientSize = placement.clientSize;
        return Result::Success;
    }

    WindowPlacement Window::GetPlacement() const
    {
        return {GetPosition(), GetClientSize()};
    }

    Result Window::SetMinMaxClientSize(LogicalSize minimum, LogicalSize maximum)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (!ValidSizeLimits(minimum, maximum))
            return Result::InvalidArgument;
        impl_->backend->setMinMaxSize(static_cast<Size>(minimum), static_cast<Size>(maximum));
        impl_->config.minClientSize = minimum;
        impl_->config.maxClientSize = maximum;
        return Result::Success;
    }

    LogicalSize Window::GetMinClientSize() const
    {
        if (!IsCreated())
            return impl_->config.minClientSize;
        const Size size = impl_->backend->getMinSize();
        return {size.x, size.y};
    }
    LogicalSize Window::GetMaxClientSize() const
    {
        if (!IsCreated())
            return impl_->config.maxClientSize;
        const Size size = impl_->backend->getMaxSize();
        return {size.x, size.y};
    }

    Result Window::Center(CenterTarget target)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (GetBackendId() == BackendId::Wayland && impl_->parent == nullptr)
            return Result::NotSupported;

        Rect area;
        if (target == CenterTarget::Parent)
        {
            if (impl_->parent == nullptr)
                return Result::InvalidState;
            const LogicalSize parentSize = impl_->parent->GetClientSize();
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
            if (GetBackendId() == BackendId::Win32)
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
        const LogicalSize size = GetClientSize();
        const Point origin = area.LeftTop();
        return SetPosition({origin.x + (area.GetWidth() - size.x) / 2, origin.y + (area.GetHeight() - size.y) / 2});
    }

    Result Window::SetWindowMode(WindowMode mode)
    {
        platform_.AssertCurrentThread();
        if (!IsCreated())
            return Result::InvalidState;
        if (mode == WindowMode::FullscreenAllMonitors && GetBackendId() == BackendId::Wayland)
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
        if (state == WindowShowState::Minimized && GetBackendId() == BackendId::Wayland)
            return Result::NotSupported;
        impl_->backend->setDisplayState(state);
        impl_->config.showState = state;
        return Result::Success;
    }

    WindowShowState Window::GetShowState() const
    {
        return IsCreated() ? impl_->backend->getDisplayState() : impl_->config.showState;
    }

    bool Window::IsConfigured() const
    {
        platform_.AssertCurrentThread();
        return impl_->state == Impl::State::Created && impl_->configured;
    }

    Result Window::RequestActivation()
    {
        if (!IsCreated())
            return Result::InvalidState;
        if (GetBackendId() != BackendId::Win32 || impl_->parent != nullptr)
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
        impl_->backend->setWindowStyles(impl_->backend->getWindowStyles(), false);
        impl_->backend->setWindowStyles(styles.get(), true);
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

    bool Window::GetAlwaysOnTop() const
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

    bool Window::GetTransparent() const
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

    bool Window::GetEraseBackground() const
    {
        return IsCreated() ? impl_->backend->getEraseBackground() : impl_->config.eraseBackground;
    }

    Result Window::EnableDragAndDrop(bool enable)
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
        if (GetBackendId() != BackendId::Win32)
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
        if (impl_->state == Impl::State::Destroyed || impl_->state == Impl::State::Destroying)
            return Result::InvalidState;
        if (cursor.IsCustom() && GetBackendId() == BackendId::Wayland)
            return Result::NotSupported;
        if (impl_->state == Impl::State::PreCreate)
        {
            impl_->cursor = std::move(cursor);
            return Result::Success;
        }
        if (impl_->cursorBackend == nullptr)
        {
            auto nativeCursor = internal::createDefaultCursorBackend();
            impl_->cursorBackend = std::shared_ptr<internal::ICursorBackend>(std::move(nativeCursor));
            impl_->backend->setCursor(impl_->cursorBackend);
        }
        const Result result = cursor.IsCustom()
                                  ? impl_->cursorBackend->setCustomCursor(cursor.BitmapData(), cursor.Hotspot())
                                  : (impl_->cursorBackend->setCursorShape(cursor.Shape()), Result::Success);
        if (result == Result::Success)
        {
            impl_->cursor = std::move(cursor);
            impl_->cursorBackend->setVisible(impl_->cursorVisible);
        }
        return result;
    }

    Result Window::ResetMouseCursor()
    {
        platform_.AssertCurrentThread();
        if (impl_->state == Impl::State::Destroyed || impl_->state == Impl::State::Destroying)
            return Result::InvalidState;
        return SetMouseCursor(Cursor::FromShape(CursorShape::Arrow));
    }

    Result Window::SetMouseCursorVisible(bool visible)
    {
        platform_.AssertCurrentThread();
        if (impl_->state == Impl::State::Destroyed || impl_->state == Impl::State::Destroying)
            return Result::InvalidState;
        impl_->cursorVisible = visible;
        if (!impl_->cursor.has_value())
            return SetMouseCursor(Cursor::FromShape(CursorShape::Arrow));
        if (impl_->cursorBackend != nullptr)
            impl_->cursorBackend->setVisible(visible);
        return Result::Success;
    }

    Result Window::SetWindowIcon(WindowIcon icon)
    {
        platform_.AssertCurrentThread();
        if (icon.bitmap_ == nullptr)
            return Result::InvalidArgument;
        if (impl_->state == Impl::State::Destroyed || impl_->state == Impl::State::Destroying)
            return Result::InvalidState;
        if (GetBackendId() != BackendId::Win32)
            return Result::NotSupported;
        if (impl_->state == Impl::State::PreCreate)
        {
            impl_->icon = std::move(icon);
            return Result::Success;
        }
        const BitmapBuffer bitmap = icon.GetBuffer();
        const Result result = impl_->backend->setWindowIcon(&bitmap);
        if (result == Result::Success)
            impl_->icon = std::move(icon);
        return result;
    }

    Result Window::ResetWindowIcon()
    {
        platform_.AssertCurrentThread();
        if (impl_->state == Impl::State::Destroyed || impl_->state == Impl::State::Destroying)
            return Result::InvalidState;
        if (impl_->state == Impl::State::PreCreate)
        {
            impl_->icon.reset();
            return Result::Success;
        }
        const Result result = impl_->backend->setWindowIcon(nullptr);
        if (result == Result::Success)
            impl_->icon.reset();
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
        if (impl_->listeners->IsClosed())
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
            impl_->state = Impl::State::Destroying;
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
