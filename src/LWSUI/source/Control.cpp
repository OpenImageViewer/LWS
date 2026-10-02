#include <LWSUI/Control.hpp>
#include <LWSUI/UIHost.hpp>
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    Control::Control() : lifetime_(std::make_shared<ControlHandle::Lifetime>(ControlHandle::Lifetime{this})) {}
    Control::~Control()
    {
        if (host_)
            host_->Detached(*this);
        lifetime_->control = nullptr;
    }
    ControlHandle Control::Handle() const
    {
        ControlHandle h;
        h.lifetime_ = lifetime_;
        return h;
    }
    const Theme& Control::Style() const
    {
        static const Theme defaults;
        return style_ ? *style_ : parent_ ? parent_->Style() : host_ ? host_->Style() : defaults;
    }
    const FontSpec& Control::Font() const
    {
        switch (fontRole)
        {
            case FontRole::Small:
                return Style().smallFont;
            case FontRole::Heading:
                return Style().headingFont;
            case FontRole::Title:
                return Style().titleFont;
            case FontRole::Glyph:
                return Style().glyphFont;
            default:
                return Style().font;
        }
    }
    std::string_view Control::HighlightPattern() const
    {
        return highlightPattern_ ? std::string_view(*highlightPattern_)
               : parent_         ? parent_->HighlightPattern()
                                 : std::string_view{};
    }
    std::vector<TextSpan> Control::HighlightSpans(std::string_view text) const
    {
        std::vector<TextSpan> spans;
        for (auto range : FindTextMatches(text, HighlightPattern()))
            spans.push_back({range, Style().searchForeground, Style().searchBackground});
        return spans;
    }
    void Control::DrawText(Canvas& canvas, std::string_view text, float x, float y, float width, float height,
                           LLUtils::Color color, bool wrap, bool highlight) const
    {
        canvas.Text(text, x, y, width, height, color, Font(), wrap,
                    highlight ? HighlightSpans(text) : std::vector<TextSpan>{});
    }
    LLUtils::Color Control::Foreground() const
    {
        return foreground_.value_or(style_ ? style_->foreground : parent_ ? parent_->Foreground() : Style().foreground);
    }
    Size Control::Measure(Size available)
    {
        desired_ = visible_ ? ConstrainSize(OnMeasure(ConstrainSize(available))) : Size{};
        return desired_;
    }
    Size Control::MeasureContent(Size available)
    {
        desired_ = visible_ ? ConstrainSize(OnMeasureContent(ConstrainSize(available))) : Size{};
        return desired_;
    }
    void Control::SetMaxWidth(std::optional<float> width)
    {
        if (width && (!std::isfinite(*width) || *width < 0))
            throw std::invalid_argument("Invalid maximum control width");
        if (maxWidth_ == width)
            return;
        maxWidth_ = width;
        Invalidate(true);
    }
    void Control::SetMaxHeight(std::optional<float> height)
    {
        if (height && (!std::isfinite(*height) || *height < 0))
            throw std::invalid_argument("Invalid maximum control height");
        if (maxHeight_ == height)
            return;
        maxHeight_ = height;
        Invalidate(true);
    }
    Size Control::ConstrainSize(Size size) const
    {
        if (maxWidth_)
            size.width = std::min(size.width, *maxWidth_);
        if (maxHeight_)
            size.height = std::min(size.height, *maxHeight_);
        return size;
    }
    Size Control::OnMeasure(Size available)
    {
        const float line = Host() ? Host()->MeasureText("Mg", available.width, false, Font()).height
                                  : Font().size * 1.4f;
        return {available.width, std::max(Style().rowHeight, line + 2 * Style().textPadding)};
    }
    void Control::Arrange(Rect bounds)
    {
        const auto size = ConstrainSize({bounds.width, bounds.height});
        bounds_ = {bounds.x, bounds.y, size.width, size.height};
        OnArrange();
    }
    void Control::Render(Canvas& canvas)
    {
        if (!visible_ || bounds_.width <= 0 || bounds_.height <= 0 ||
            canvas.OutsideClip(bounds_.x, bounds_.y, bounds_.width, bounds_.height))
            return;
        Canvas::ClipScope clip(canvas, bounds_.x, bounds_.y, bounds_.width, bounds_.height);
        OnRender(canvas);
    }
    bool Control::Dispatch(const Input& input)
    {
        return visible_ && enabled_ && OnInput(input);
    }
    Control* Control::HitTest(float x, float y)
    {
        return visible_ && enabled_ && hitTestVisible && bounds_.Contains(x, y) ? this : nullptr;
    }
    void Control::SetVisible(bool value)
    {
        if (visible_ != value)
        {
            if (!value)
                Finish(EditPhase::Cancel);
            visible_ = value;
            Invalidate(true);
        }
    }
    void Control::SetEnabled(bool value)
    {
        if (!value)
            Finish(EditPhase::Cancel);
        enabled_ = value;
        Invalidate();
    }
    void Control::Invalidate(bool layout)
    {
        if (host_)
            host_->Invalidate(layout);
    }
    void Control::Capture()
    {
        if (host_)
            host_->Capture(this);
    }
    void Control::ReleaseCapture()
    {
        if (host_)
            host_->ReleaseCapture(this);
    }
    void Control::Attach(UIHost* host, Container* parent)
    {
        const bool changed = host_ != host;
        if (changed)
        {
            if (host_)
            {
                Finish(EditPhase::Cancel);
                OnDetach();
                host_->Detached(*this);
            }
            lifetime_->control = nullptr;
            lifetime_ = std::make_shared<ControlHandle::Lifetime>(ControlHandle::Lifetime{this});
            host_ = host;
            hovered_ = false;
        }
        parent_ = parent;
        if (auto* container = dynamic_cast<Container*>(this))
            for (auto& child : container->children_)
                child->Attach(host, container);
        if (changed && host_)
            OnAttach();
    }
    Control& Container::Add(std::unique_ptr<Control> child)
    {
        child->Attach(Host(), this);
        auto& result = *child;
        children_.push_back(std::move(child));
        Invalidate(true);
        return result;
    }
    std::unique_ptr<Control> Container::Remove(Control& child)
    {
        auto it = std::find_if(children_.begin(), children_.end(), [&](auto& p) { return p.get() == &child; });
        if (it == children_.end())
            return {};
        return RemoveAt(size_t(it - children_.begin()));
    }
    std::unique_ptr<Control> Container::RemoveAt(size_t index)
    {
        children_[index]->Finish(EditPhase::Cancel);
        children_[index]->Attach(nullptr, nullptr);
        auto result = std::move(children_[index]);
        children_.erase(children_.begin() + index);
        Invalidate(true);
        return result;
    }
    void Container::Clear()
    {
        while (!children_.empty())
            RemoveAt(children_.size() - 1);
    }
    Control* Container::HitTest(float x, float y)
    {
        if (!Control::HitTest(x, y))
            return nullptr;
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
            if (auto* hit = (*it)->HitTest(x, y))
                return hit;
        return hitTestBackground ? this : nullptr;
    }
    bool Container::OnPreviewInput(const Input& input)
    {
        for (auto it = children_.rbegin(); it != children_.rend(); ++it)
        {
            // Pointer previews obey ancestor clipping just like normal hit testing.
            if (input.kind == InputKind::Down && !(*it)->Bounds().Contains(input.x, input.y))
                continue;
            if ((*it)->PreviewInput(input))
                return true;
        }
        return false;
    }
    void Container::OnRender(Canvas& c)
    {
        for (auto& child : children_)
            child->Render(c);
    }
    bool Container::Finish(EditPhase phase)
    {
        for (auto& child : children_)
            if (!child->Finish(phase))
                return false;
        return true;
    }
}  // namespace LWSUI
