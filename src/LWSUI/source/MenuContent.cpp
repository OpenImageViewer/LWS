#include "MenuHost.hpp"
#include <LWSUI/UIHost.hpp>
#include <algorithm>
#include <cmath>

namespace LWSUI
{
    void MenuBar::SetIcon(LWS::BitmapSharedPtr icon)
    {
        if (icon_ == icon)
            return;
        icon_ = std::move(icon);
        if (Host())
            Host()->CloseMainMenu();
        Invalidate(true);
    }
    float MenuBar::IconSpan() const
    {
        return icon_ ? std::min(24.f, std::max(0.f, Style().menuBarHeight - 8)) + 16 : 0;
    }
    float MenuBar::ItemsSpan() const
    {
        float span = 0;
        for (size_t i = 0; i < items_.size(); ++i)
            span += ItemExtent(i);
        return span;
    }
    bool MenuBar::ContentActive() const
    {
        return content_ && orientation == Orientation::Horizontal && Host() && Host()->MainMenu() == this &&
               (Host()->MainMenuDock() == MenuDock::Top || Host()->MainMenuDock() == MenuDock::Bottom);
    }
    void MenuBar::SetContent(std::unique_ptr<Control> content, float minimumWidth)
    {
        if (Host())
            Host()->CloseMainMenu();
        content_ = nullptr;
        Clear();
        contentMinimum_ = std::isfinite(minimumWidth) ? std::max(0.f, minimumWidth) : 0;
        if (content)
            content_ = &Add(std::move(content));
        Invalidate(true);
    }
    void MenuBar::SetContentMinimumWidth(float width)
    {
        width = std::isfinite(width) ? std::max(0.f, width) : 0;
        if (contentMinimum_ == width)
            return;
        contentMinimum_ = width;
        Invalidate(true);
    }
    float MenuBar::MinimumWidth() const
    {
        if (orientation != Orientation::Horizontal)
            return Thickness();
        return IconSpan() + ItemsSpan() + (ContentActive() ? contentMinimum_ : 0) + WindowButtonsSpan();
    }
    void MenuBar::NotifyMinimumWidth()
    {
        const float value = MinimumWidth();
        if (value == notifiedMinimum_)
            return;
        notifiedMinimum_ = value;
        OnMinimumWidthChanged.Raise(value);
    }
    Rect MenuBar::ContentRect() const
    {
        return CalculateLayout().content;
    }
    Control* MenuBar::HitContent(float x, float y) const
    {
        return Enabled() && Visible() && ContentActive() && ContentRect().Contains(x, y) ? content_->HitTest(x, y) : nullptr;
    }
    bool MenuBar::ContentOccupied(float x, float y) const
    {
        if (!ContentActive() || !ContentRect().Contains(x, y))
            return false;
        if (HitContent(x, y))
            return true;
        // Disabled controls still occupy their rectangles, although normal hit-testing rejects input.
        const auto disabled = [&](auto&& self, Control& control) -> bool
        {
            if (!control.Visible() || !control.Bounds().Contains(x, y))
                return false;
            if (!control.Enabled())
                return true;
            if (auto* group = dynamic_cast<Container*>(&control))
                for (auto& child : group->Children())
                    if (self(self, *child))
                        return true;
            return false;
        };
        return disabled(disabled, *content_);
    }
    bool MenuBar::InContent(Control* control) const
    {
        for (; control; control = control->Parent())
            if (control == content_)
                return true;
        return false;
    }
    void MenuBar::ArrangeContent()
    {
        if (ContentActive())
        {
            const auto b = ContentRect();
            content_->Measure({b.width, b.height});
            content_->Arrange(b);
        }
        else if (content_)
        {
            content_->Finish(EditPhase::Cancel);
            if (Host() && Host()->HasFocusWithin(*content_))
                Host()->Focus(nullptr);
            content_->Arrange({});
        }
        NotifyMinimumWidth();
    }
    void MenuBar::RenderContent(Canvas& canvas, const Layout& layout)
    {
        if (icon_)
        {
            const auto r = layout.icon;
            const auto bitmap = icon_->GetBuffer();
            const float side = std::max(0.f, std::min({24.f, r.width - 8, r.height - 8}));
            if (side > 0 && bitmap.width && bitmap.height)
            {
                const float scale = side / float(std::max(bitmap.width, bitmap.height));
                const float width = bitmap.width * scale, height = bitmap.height * scale;
                canvas.Image(bitmap, r.x + (r.width - width) / 2, r.y + (r.height - height) / 2, width, height);
            }
        }
        if (ContentActive())
        {
            const auto r = layout.content;
            Canvas::ClipScope clip(canvas, r.x, r.y, r.width, r.height);
            content_->Render(canvas);
        }
    }
}  // namespace LWSUI
