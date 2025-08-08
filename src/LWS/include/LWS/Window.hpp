#pragma once
#include <LWS/Event.hpp>
#include <LWS/Result.hpp>
#include <LWS/interfaces/backends.hpp>

#include <LLUtils/BitFlags.h>
#include <LLUtils/Color.h>
#include <LLUtils/EnumClassBitwise.h>

#include <filesystem>
#include <memory>
#include <vector>

namespace LWS
{
    LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(WindowStyle)
    using WindowStyleFlags = LLUtils::BitFlags<WindowStyle>;

    class Cursor;
    class Window;
    using VecChildWindows = std::vector<Window*>;

    class Window
    {
    public:
        Window();
        explicit Window(std::unique_ptr<IWindowBackend> impl);
        virtual ~Window();

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) noexcept = default;
        Window& operator=(Window&&) noexcept = default;

        [[nodiscard]] Result Create(const WindowConfig& config = {});
        void Destroy();

        void SetTitle(const LWS::string_type& title);
        [[nodiscard]] LWS::string_type GetTitle() const;

        void SetWindowIcon(const std::filesystem::path& iconPath);

        void SetVisible(bool visible);
        [[nodiscard]] bool GetVisible() const;
        void SetDisplayState(WindowDisplayState state);
        [[nodiscard]] WindowDisplayState GetDisplayState() const;

        void SetPosition(const Point& position);
        [[nodiscard]] Point GetPosition() const;
        void SetSize(const Size& sz);
        [[nodiscard]] Size GetClientSize() const;
        [[nodiscard]] Rect GetClientRect() const;
        [[nodiscard]] Size GetWindowSize() const;
        void SetPlacement(const WindowPlacement& placement);
        [[nodiscard]] WindowPlacement GetPlacement() const;
        void SetMinMaxSize(Size minSize, Size maxSize);
        [[nodiscard]] Size GetMinSize() const;
        [[nodiscard]] Size GetMaxSize() const;
        void Move(const Point& delta);

        void CenterOnScreen();
        void CenterOnParent();

        void SetWindowStyles(WindowStyle styles, bool enable);
        [[nodiscard]] WindowStyleFlags GetWindowStyles() const;

        void SetForeground();
        void SetFocused();
        [[nodiscard]] bool IsInFocus() const;

        void SetAlwaysOnTop(bool onTop);
        [[nodiscard]] bool GetAlwaysOnTop() const;
        void SetTransparent(bool transparent);
        [[nodiscard]] bool GetTransparent() const;

        void SetBackgroundColor(LLUtils::Color color);
        void SetEraseBackground(bool erase);
        [[nodiscard]] bool GetEraseBackground() const;

        void ToggleFullScreen(bool multiMonitor = false);
        void SetFullScreenState(FullScreenState state);
        [[nodiscard]] FullScreenState GetFullScreenState() const;
        [[nodiscard]] bool IsFullScreen() const;

        [[nodiscard]] bool IsMouseInClientRect() const;
        [[nodiscard]] bool IsUnderMouseCursor() const;
        [[nodiscard]] Point GetMousePosition() const;
        void SetDoubleClickMode(DoubleClickMode mode);
        [[nodiscard]] DoubleClickMode GetDoubleClickMode() const;
        void SetLockMouseToWindowMode(LockMouseToWindowMode mode);
        [[nodiscard]] LockMouseToWindowMode GetLockMouseToWindowMode() const;

        void SetMouseCursor(Cursor* cursor);
        [[nodiscard]] Cursor* GetMouseCursor() const;

        void SetParent(Window* parent);
        [[nodiscard]] Window* GetParent() const;

        void EnableDragAndDrop(bool enable);

        void SetDestroyOnClose(bool destroyOnClose);

        [[nodiscard]] EventListenerToken AddEventListener(EventCallback callback);
        void RemoveEventListener(EventListenerToken token);
        [[nodiscard]] EventListenerGuard MakeListenerGuard(EventListenerToken token);

        void InjectRawEvent(void* platformEvent);

        [[nodiscard]] BackendId GetBackendId() const;
        [[nodiscard]] Handle GetHandle() const;

    protected:
        template<typename T>
        T* getBackendAs() { return dynamic_cast<T*>(impl_.get()); }

        template<typename T>
        const T* getBackendAs() const { return dynamic_cast<const T*>(impl_.get()); }

        std::unique_ptr<IWindowBackend> impl_;

    private:
        Window* fParent = nullptr;
        VecChildWindows fChildren;
        Cursor* fMouseCursor = nullptr;
        bool fDestroyOnClose = true;

        void AddChild(Window* child);
        void RemoveChild(Window* child);
        void NotifyRemovedFromRelatedWindows();
    };
}
