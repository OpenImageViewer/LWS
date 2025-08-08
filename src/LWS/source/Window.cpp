#include <LWS/Window.hpp>
#include <LWS/Cursor.hpp>
#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace LWS
{
    Window::Window()
        : impl_(internal::createDefaultWindowBackend())
    {
    }

    Window::Window(std::unique_ptr<IWindowBackend> impl)
        : impl_(std::move(impl))
    {
    }

    Window::~Window()
    {
        Destroy();
    }

    Result Window::Create(const WindowConfig& config)
    {
        if (!impl_)
        {
            return Result::InvalidState;
        }

        return impl_->create(config);
    }

    void Window::Destroy()
    {
        if (!impl_)
        {
            return;
        }

        VecChildWindows copy_of_children = fChildren;
        for (Window* child : copy_of_children)
        {
            child->Destroy();
        }

        NotifyRemovedFromRelatedWindows();
        impl_->destroy();
    }

    void Window::SetTitle(const LWS::string_type& title) { impl_->setTitle(title); }
    LWS::string_type Window::GetTitle() const { return impl_->getTitle(); }
    void Window::SetWindowIcon(const std::filesystem::path& path) { impl_->setWindowIcon(path); }
    void Window::SetVisible(bool visible) { visible ? impl_->show() : impl_->hide(); }
    bool Window::GetVisible() const { return impl_->getVisible(); }
    void Window::SetDisplayState(WindowDisplayState state) { impl_->setDisplayState(state); }
    WindowDisplayState Window::GetDisplayState() const { return impl_->getDisplayState(); }
    void Window::SetPosition(const Point& position) { impl_->setPosition(position); }
    Point Window::GetPosition() const { return impl_->getPosition(); }
    void Window::SetSize(const Size& size) { impl_->setSize(size); }
    Size Window::GetClientSize() const { return impl_->getClientSize(); }
    Rect Window::GetClientRect() const { return impl_->getClientRect(); }
    Size Window::GetWindowSize() const { return impl_->getWindowSize(); }
    void Window::SetPlacement(const WindowPlacement& placement) { impl_->setPlacement(placement); }
    WindowPlacement Window::GetPlacement() const { return impl_->getPlacement(); }
    void Window::SetMinMaxSize(Size min_size, Size max_size) { impl_->setMinMaxSize(min_size, max_size); }
    Size Window::GetMinSize() const { return impl_->getMinSize(); }
    Size Window::GetMaxSize() const { return impl_->getMaxSize(); }

    void Window::Move(const Point& delta)
    {
        Point position = GetPosition();
        SetPosition({ position.x + delta.x, position.y + delta.y });
    }

    void Window::CenterOnScreen()
    {
        Size window_size = GetWindowSize();
        SetPosition({ (1920 - window_size.x) / 2, (1080 - window_size.y) / 2 });
    }

    void Window::CenterOnParent()
    {
        if (!fParent)
        {
            CenterOnScreen();
            return;
        }

        Size parent_size = fParent->GetWindowSize();
        Point parent_position = fParent->GetPosition();
        Size my_size = GetWindowSize();
        SetPosition({
            parent_position.x + (parent_size.x - my_size.x) / 2,
            parent_position.y + (parent_size.y - my_size.y) / 2,
        });
    }

    void Window::SetWindowStyles(WindowStyle styles, bool enable) { impl_->setWindowStyles(styles, enable); }
    WindowStyleFlags Window::GetWindowStyles() const { return WindowStyleFlags(impl_->getWindowStyles()); }
    void Window::SetForeground() { impl_->setForeground(); }
    void Window::SetFocused() { impl_->setFocus(); }
    bool Window::IsInFocus() const { return impl_->isInFocus(); }
    void Window::SetAlwaysOnTop(bool on_top) { impl_->setAlwaysOnTop(on_top); }
    bool Window::GetAlwaysOnTop() const { return impl_->getAlwaysOnTop(); }
    void Window::SetTransparent(bool transparent) { impl_->setTransparent(transparent); }
    bool Window::GetTransparent() const { return impl_->getTransparent(); }
    void Window::SetBackgroundColor(LLUtils::Color color) { impl_->setBackgroundColor(color); }
    void Window::SetEraseBackground(bool erase) { impl_->setEraseBackground(erase); }
    bool Window::GetEraseBackground() const { return impl_->getEraseBackground(); }
    void Window::ToggleFullScreen(bool multi_monitor) { impl_->toggleFullScreen(multi_monitor); }
    void Window::SetFullScreenState(FullScreenState state) { impl_->setFullScreenState(state); }
    FullScreenState Window::GetFullScreenState() const { return impl_->getFullScreenState(); }
    bool Window::IsFullScreen() const { return impl_->isFullScreen(); }
    bool Window::IsMouseInClientRect() const { return impl_->isMouseInClientRect(); }
    bool Window::IsUnderMouseCursor() const { return impl_->isUnderMouseCursor(); }
    Point Window::GetMousePosition() const { return impl_->getMousePosition(); }
    void Window::SetDoubleClickMode(DoubleClickMode mode) { impl_->setDoubleClickMode(mode); }
    DoubleClickMode Window::GetDoubleClickMode() const { return impl_->getDoubleClickMode(); }
    void Window::SetLockMouseToWindowMode(LockMouseToWindowMode mode) { impl_->setLockMouseToWindowMode(mode); }
    LockMouseToWindowMode Window::GetLockMouseToWindowMode() const { return impl_->getLockMouseToWindowMode(); }

    void Window::SetMouseCursor(Cursor* cursor)
    {
        fMouseCursor = cursor;
        if (cursor)
        {
            impl_->setCursor(cursor->getBackendShared());
        }
        else
        {
            impl_->setCursor(nullptr);
        }
    }

    Cursor* Window::GetMouseCursor() const { return fMouseCursor; }

    void Window::SetParent(Window* parent)
    {
        if (fParent)
        {
            fParent->RemoveChild(this);
        }

        fParent = parent;
        if (fParent)
        {
            fParent->AddChild(this);
        }

        impl_->setParent(parent ? parent->impl_.get() : nullptr);
    }

    Window* Window::GetParent() const { return fParent; }
    void Window::EnableDragAndDrop(bool enable) { impl_->enableDragAndDrop(enable); }
    void Window::SetDestroyOnClose(bool destroy_on_close) { fDestroyOnClose = destroy_on_close; }
    EventListenerToken Window::AddEventListener(EventCallback callback) { return impl_->addListener(std::move(callback)); }
    void Window::RemoveEventListener(EventListenerToken token) { impl_->removeListener(token); }
    EventListenerGuard Window::MakeListenerGuard(EventListenerToken token) { return { this, token }; }
    void Window::InjectRawEvent(void* platform_event) { impl_->injectRawEvent(platform_event); }
    BackendId Window::GetBackendId() const { return impl_->backend(); }
    Handle Window::GetHandle() const { return impl_->getHandle(); }

    void Window::AddChild(Window* child) { fChildren.push_back(child); }

    void Window::RemoveChild(Window* child)
    {
        std::erase(fChildren, child);
    }

    void Window::NotifyRemovedFromRelatedWindows()
    {
        if (fParent)
        {
            fParent->RemoveChild(this);
            fParent = nullptr;
        }

        for (Window* child : fChildren)
        {
            child->fParent = nullptr;
        }

        fChildren.clear();
    }

    EventListenerGuard& EventListenerGuard::operator=(EventListenerGuard&& other) noexcept
    {
        if (this != &other)
        {
            if (window)
            {
                window->RemoveEventListener(token);
            }

            window = other.window;
            token = other.token;
            other.window = nullptr;
        }

        return *this;
    }

    EventListenerGuard::~EventListenerGuard()
    {
        if (window)
        {
            window->RemoveEventListener(token);
        }
    }
}
