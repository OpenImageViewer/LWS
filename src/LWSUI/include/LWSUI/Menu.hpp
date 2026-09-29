#pragma once
#include <LWSUI/Control.hpp>
#include <functional>
#include <optional>
#include <string>
#include <vector>
namespace LWSUI
{
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
    class MenuBar : public Control
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
        void SetItems(std::vector<MenuBarItem> items);
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

      private:

        std::vector<Rect> ItemRects() const;
        float ItemExtent(size_t index) const;
        float TextWidth(std::string_view text) const;
        float TextHeight(std::string_view text) const;
        std::vector<MenuBarItem> items_;
        float span_ = 0, minSpan_ = 0, maxSpan_ = 0;
    };
}  // namespace LWSUI
