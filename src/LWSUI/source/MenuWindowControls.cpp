#include "MenuHost.hpp"
#include <LWSUI/UIHost.hpp>
#include <LWS/Platform.hpp>
#include <algorithm>
#include <array>

namespace LWSUI
{
    namespace
    {
        using Action = internal::MenuWindowAction;
        constexpr std::array actions{Action::Minimize, Action::Maximize, Action::Close};
        bool SameRect(Rect a, Rect b)
        {
            return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
        }
    }  // namespace
    void MenuBar::SetWindowControls(MenuBarWindowControls controls)
    {
        if (Host())
            Host()->CloseMainMenu();
        windowControls_ = std::move(controls);
        Invalidate(true);
    }
    bool MenuBar::WindowControlsAvailable() const
    {
        if (!Host() || Host()->MainMenu() != this || orientation != Orientation::Horizontal)
            return false;
        const auto dock = Host()->MainMenuDock();
        auto& window = Host()->Window();
        return (dock == MenuDock::Top || dock == MenuDock::Bottom) && window.IsCreated() && !window.IsPopup() &&
               window.GetParent() == nullptr;
    }
    bool MenuBar::HasWindowButton(WindowAction action) const
    {
        if (!WindowControlsAvailable())
            return false;
        switch (action)
        {
            case WindowAction::Minimize:
                return windowControls_.minimize;
            case WindowAction::Maximize:
                return windowControls_.maximize;
            case WindowAction::Close:
                return bool(windowControls_.requestClose);
        }
        return false;
    }
    bool MenuBar::WindowButtonEnabled(WindowAction action) const
    {
        return Enabled() && Visible() && HasWindowButton(action) &&
               (action != WindowAction::Minimize ||
                Host()->Window().GetPlatformContext().GetBackendId() != LWS::BackendId::Wayland);
    }
    float MenuBar::WindowButtonsSpan() const
    {
        float width = 0;
        for (auto action : actions)
            if (HasWindowButton(action))
                width += std::max(Style().menuBarHeight, Font().size * 2);
        return width;
    }
    float MenuBar::DragSpan() const
    {
        return WindowControlsAvailable() && windowControls_.draggable && !ContentActive() ? 48.f : 0.f;
    }
    float MenuBar::WindowControlsSpan() const
    {
        return WindowButtonsSpan() + DragSpan();
    }
    Rect MenuBar::WindowButtonRect(WindowAction action) const
    {
        return CalculateLayout().buttons[size_t(action)];
    }
    std::optional<MenuBar::WindowAction> MenuBar::HitWindowButton(float x, float y) const
    {
        const auto layout = CalculateLayout();
        for (auto action : actions)
            if (layout.buttons[size_t(action)].Contains(x, y))
                return action;
        return std::nullopt;
    }
    Rect MenuBar::WindowDragArea() const
    {
        return Enabled() && Visible() && WindowControlsAvailable() && windowControls_.draggable ? CalculateLayout().drag
                                                                                                : Rect{};
    }
    bool MenuBar::HitWindowDrag(float x, float y) const
    {
        if (!Enabled() || !Visible() || !WindowControlsAvailable() || !windowControls_.draggable)
            return false;
        const auto layout = CalculateLayout();
        const auto contains = [x, y](Rect rect) { return rect.Contains(x, y); };
        return (contains(layout.icon) || contains(layout.drag)) && std::ranges::none_of(layout.items, contains) &&
               std::ranges::none_of(layout.buttons, contains) && !ContentOccupied(x, y);
    }
    void MenuBar::ClearWindowPress()
    {
        const bool pressed = windowPressed_.has_value();
        windowPressed_.reset();
        windowHover_.reset();
        if (pressed)
            ReleaseCapture();
        Invalidate();
    }
    void MenuBar::OnArrange()
    {
        ArrangeContent();
        if (windowPressed_ && !SameRect(WindowButtonRect(*windowPressed_), windowPressBounds_))
            ClearWindowPress();
    }
    void MenuBar::RenderWindowControls(Canvas& canvas, const Layout& layout)
    {
        for (auto action : actions)
        {
            if (!HasWindowButton(action))
                continue;
            const auto r = layout.buttons[size_t(action)];
            if (r.width <= 0 || r.height <= 0)
                continue;
            Canvas::ClipScope clip(canvas, r.x, r.y, r.width, r.height);
            const bool enabled = WindowButtonEnabled(action);
            const bool over = enabled && windowHover_ == action;
            const bool down = over && windowPressed_ == action;
            if (over)
                canvas.Fill(r.x, r.y, r.width, r.height, down ? Style().selection : Style().hoverSurface);
            const auto color = !enabled ? Style().muted : down ? Style().selectionForeground : Style().foreground;
            const float unit = std::min(r.width, r.height) * .26f;
            const float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
            const float left = cx - unit / 2, top = cy - unit / 2;
            const auto box = [&](float x, float y, float size)
            {
                canvas.Line(x, y, x + size, y, 1, color);
                canvas.Line(x, y, x, y + size, 1, color);
                canvas.Line(x + size, y, x + size, y + size, 1, color);
                canvas.Line(x, y + size, x + size, y + size, 1, color);
            };
            if (action == WindowAction::Minimize)
                canvas.Line(left, cy, left + unit, cy, 1, color);
            else if (action == WindowAction::Close)
            {
                canvas.Line(left, top, left + unit, top + unit, 1, color);
                canvas.Line(left + unit, top, left, top + unit, 1, color);
            }
            else if (Host()->Window().GetShowState() == LWS::WindowShowState::Maximized)
            {
                box(left + unit * .25f, top, unit * .75f);
                box(left, top + unit * .25f, unit * .75f);
            }
            else
                box(left, top, unit);
            if (windowFocus_ == action)
                canvas.Line(r.x + 4, r.y + r.height - 3, r.x + r.width - 4, r.y + r.height - 3, Style().focusWidth,
                            Style().accent);
        }
    }
    namespace internal
    {
        void MenuSession::ActivateWindowButton(MenuWindowAction action)
        {
            auto* bar = host_.MainMenu();
            if (!bar || !bar->WindowButtonEnabled(action))
                return;
            const auto handle = bar->Handle();
            auto close = bar->windowControls_.requestClose;
            Close();
            const auto result = host_.Window().GetPlatformContext().PostTask(
                [handle, action, close = std::move(close)]
                {
                    auto* bar = static_cast<MenuBar*>(handle.Get());
                    if (!bar || !bar->Host() || !bar->WindowButtonEnabled(action))
                        return;
                    if (action == MenuWindowAction::Close)
                    {
                        if (close)
                            close();
                        return;
                    }
                    auto& window = bar->Host()->Window();
                    const auto target = action == MenuWindowAction::Minimize ? LWS::WindowShowState::Minimized
                                        : window.GetShowState() == LWS::WindowShowState::Maximized
                                            ? LWS::WindowShowState::Restored
                                            : LWS::WindowShowState::Maximized;
                    const auto result = window.RequestShowState(target);
                    // State-change listeners can retire the bar while the native request is in progress.
                    if (result != LWS::Result::Success && handle && handle.Get()->Host())
                        handle.Get()->Host()->ReportError("Cannot change window show state");
                });
            if (result != LWS::Result::Success)
                host_.ReportError("Cannot schedule menu window action");
        }
        bool MenuSession::WindowInput(const Input& input)
        {
            auto* bar = host_.MainMenu();
            if (!bar)
                return false;
            if (bar->windowPressed_ && !bar->WindowButtonEnabled(*bar->windowPressed_))
                bar->ClearWindowPress();
            if (input.kind == InputKind::Cancel ||
                (input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Escape && bar->windowPressed_))
            {
                const bool pressed = bar->windowPressed_.has_value();
                bar->ClearWindowPress();
                return pressed;
            }
            const auto hit = bar->HitWindowButton(input.x, input.y);
            if (input.kind == InputKind::Move)
            {
                if (hit && !active_)
                    SetHot(-1);
                if (bar->windowHover_ != hit)
                {
                    bar->windowHover_ = hit;
                    bar->Invalidate();
                }
                return hit.has_value() || bar->windowPressed_.has_value();
            }
            if (input.kind != InputKind::Down && input.kind != InputKind::Up)
                return false;
            if (input.button != LWS::MouseButton::Left)
                return hit.has_value();
            if (input.kind == InputKind::Up && bar->windowPressed_)
            {
                const auto pressed = bar->windowPressed_;
                bar->ClearWindowPress();
                if (hit == pressed)
                    ActivateWindowButton(*pressed);
                return true;
            }
            if (input.kind == InputKind::Down && hit)
            {
                Close();
                if (bar->WindowButtonEnabled(*hit))
                {
                    if (auto* captured = host_.capture_.Get())
                    {
                        captured->Dispatch({InputKind::Cancel});
                        host_.ReleaseCapture(captured);
                    }
                    bar->windowPressed_ = hit;
                    bar->windowHover_ = hit;
                    bar->windowPressBounds_ = bar->WindowButtonRect(*hit);
                    bar->Capture();
                    bar->Invalidate();
                }
                return true;
            }
            if (input.kind == InputKind::Down && bar->HitWindowDrag(input.x, input.y))
            {
                const auto handle = bar->Handle();
                Close();
                host_.suppressPointerRelease_ = true;
                // Must be synchronous: Wayland's compositor move uses this pointer press serial.
                const auto result = host_.Window().BeginWindowDrag(LWS::WindowDragOperation::Move);
                if (result != LWS::Result::Success && handle && handle.Get()->Host())
                    handle.Get()->Host()->ReportError("Cannot start native window move");
                return true;
            }
            return hit.has_value();
        }
    }  // namespace internal
}  // namespace LWSUI
