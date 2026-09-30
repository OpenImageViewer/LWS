#pragma once
#include <LWSUI/Control.hpp>
#include <array>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace LWSUI
{
    namespace internal
    {
        class MenuSession;
        struct MenuSessionAccess;
        enum class MenuWindowAction
        {
            Minimize,
            Maximize,
            Close
        };
    }  // namespace internal
    /// Optional caption replacement for horizontal Top/Bottom bars on top-level windows.
    /// Close is shown only when requestClose is supplied. Button actions run outside input dispatch;
    /// the callback may quit, hide the window, or decline the request. Wayland minimize is disabled.
    struct MenuBarWindowControls
    {
        bool minimize = false, maximize = false, draggable = false;
        std::function<void()> requestClose;
        // Explicit opt-in, independent of native caption styles, dragging, and visible buttons.
        bool doubleClickMaximize = false;
    };
    enum class MenuDock
    {
        Floating,
        Top,
        Bottom,
        Left,
        Right
    };
    /// One dropdown row. submenu turns the row into a cascade parent; separator overrides everything.
    struct MenuItem
    {
        std::string label, shortcut;
        std::function<void()> action;
        bool enabled = true;
        std::optional<bool> checked;
        bool separator = false;
        std::vector<MenuItem> submenu;
        static MenuItem Separator();
    };
    struct MenuBarItem
    {
        std::string label;
        std::vector<MenuItem> items;
        bool enabled = true;
    };
    /// Menu bar control. A single `&` in a label marks the mnemonic that is underlined; `&&` is a
    /// literal ampersand. The bar is passive: UIHost::SetMainMenu drives state, popup dropdowns,
    /// hover and the full Windows-style keyboard path; hover the control directly for rendering.
    class MenuBar : public Container
    {
      public:

        MenuBar() = default;
        explicit MenuBar(std::vector<MenuBarItem> items) : items_(std::move(items)) {}
        Orientation orientation = Orientation::Horizontal;
        /// Highlighted item, resolved by the menu session; -1 highlights nothing.
        int hot = -1;
        /// Stretch the main axis across the available space; set by the host for docked bars.
        bool stretch = false;
        /// Draw a frame around the bar; set by the host for the floating dock.
        bool floating = false;
        /// Explicit main-axis span; 0 keeps the natural span or the docked fill.
        void SetSpan(float span)
        {
            span_ = std::max(0.f, span);
            Invalidate(true);
        }
        float Span() const { return span_; }
        void SetMinSpan(float span)
        {
            minSpan_ = std::max(0.f, span);
            Invalidate(true);
        }
        void SetMaxSpan(float span)
        {
            maxSpan_ = std::max(0.f, span);
            Invalidate(true);
        }
        void SetIcon(LWS::BitmapSharedPtr icon);
        const LWS::BitmapSharedPtr& Icon() const { return icon_; }
        // Content is active in horizontal Top/Bottom bars. Structural replacements must be deferred from callbacks.
        void SetContent(std::unique_ptr<Control> content, float minimumWidth = 0);
        Control* Content() const { return content_; }
        void SetContentMinimumWidth(float width);
        float MinimumWidth() const;
        Event<void(float)> OnMinimumWidthChanged;
        void SetItems(std::vector<MenuBarItem> items);
        void SetWindowControls(MenuBarWindowControls controls);
        const MenuBarWindowControls& WindowControls() const { return windowControls_; }
        const std::vector<MenuBarItem>& Items() const { return items_; }
        /// Cross-axis thickness for the current orientation.
        float Thickness() const;
        /// Main-axis extent of all items.
        float NaturalSpan() const;
        static constexpr size_t NoItem = static_cast<size_t>(-1);
        /// Item under a point in the bar's coordinate space; NoItem outside any item.
        size_t HitItem(float x, float y) const;
        /// Item rectangle in the bar's coordinate space.
        Rect ItemRect(size_t index) const;

      protected:

        Size OnMeasure(Size available) override;
        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;
        void OnArrange() override;

      private:

        struct Layout
        {
            Rect icon, itemsArea, content, drag;
            std::array<Rect, 3> buttons{};
            std::vector<Rect> items;
        };
        Layout CalculateLayout() const;
        friend class UIHost;
        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        bool ContentActive() const;
        Control* HitContent(float x, float y) const;
        bool ContentOccupied(float x, float y) const;
        bool InContent(Control* control) const;
        Rect ContentRect() const;
        float IconSpan() const;
        float ItemsSpan() const;
        void ArrangeContent();
        void RenderContent(Canvas& canvas, const Layout& layout);
        void NotifyMinimumWidth();
        LWS::BitmapSharedPtr icon_;
        Control* content_ = nullptr;
        float contentMinimum_ = 0, notifiedMinimum_ = -1;
        friend class internal::MenuSession;
        friend struct internal::MenuSessionAccess;
        using WindowAction = internal::MenuWindowAction;
        bool WindowControlsAvailable() const;
        bool HasWindowButton(WindowAction action) const;
        bool WindowButtonEnabled(WindowAction action) const;
        Rect WindowButtonRect(WindowAction action) const;
        std::optional<WindowAction> HitWindowButton(float x, float y) const;
        Rect WindowDragArea() const;
        bool HitWindowDrag(float x, float y) const;
        bool HitCaption(float x, float y) const;
        bool DoubleClickMaximizeAvailable() const;
        float WindowButtonsSpan() const;
        float DragSpan() const;
        float WindowControlsSpan() const;
        void RenderWindowControls(Canvas& canvas, const Layout& layout);
        void ClearWindowPress();
        MenuBarWindowControls windowControls_;
        std::optional<WindowAction> windowHover_, windowPressed_, windowFocus_;
        Rect windowPressBounds_{};
        float ItemExtent(size_t index) const;
        float TextWidth(std::string_view text) const;
        float TextHeight(std::string_view text) const;
        std::vector<MenuBarItem> items_;
        float span_ = 0, minSpan_ = 0, maxSpan_ = 0;
    };
}  // namespace LWSUI
