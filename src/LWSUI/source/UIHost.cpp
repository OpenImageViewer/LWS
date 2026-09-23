#include <LWSUI/UIHost.hpp>
#include "ContextMenu.hpp"
#include <LWS/Platform.hpp>
#include <algorithm>
#ifdef LWS_HAS_WIN32_BACKEND
    #include <LWS/Win32/WindowExtensions.hpp>
#endif
#ifdef _WIN32
    #include <windows.h>
#endif
namespace LWSUI
{
    namespace
    {
        bool Available(Control* control)
        {
            if (!control)
                return false;
            for (; control; control = control->Parent())
                if (!control->Visible() || !control->Enabled())
                    return false;
            return true;
        }
    }  // namespace
    LWS::string_type NativeText(std::string_view text)
    {
#ifdef _WIN32
        int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
        std::wstring result(size, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), result.data(), size);
        return result;
#else
        return std::string(text);
#endif
    }
    UIHost::UIHost(LWS::Window& window, Theme theme) : window_(window), theme_(theme)
    {
        auto listener = window.Listen(
            [this](const auto& e) { return Event(e) ? LWS::EventResponse::Handled : LWS::EventResponse::Unhandled; });
        if (!listener)
            throw std::runtime_error("Cannot register UI window listener");
        listener_ = std::move(*listener);
    }
    UIHost::~UIHost()
    {
        *lifetime_ = nullptr;
        listener_ = {};
        CloseContextMenu();
        popupClosed_ = {};
        popupOwner_ = {};
        if (popup_)
            popup_->Attach(nullptr, nullptr);
        if (root_)
            root_->Attach(nullptr, nullptr);
        for (auto& item : retired_)
            item->Attach(nullptr, nullptr);
    }
    void UIHost::SetRoot(std::unique_ptr<Control> root)
    {
        ClosePopup(EditPhase::Cancel);
        if (root_)
        {
            root_->Finish(EditPhase::Cancel);
            root_->Attach(nullptr, nullptr);
        }
        root_ = std::move(root);
        if (root_)
            root_->Attach(this, nullptr);
        Invalidate(true);
    }
    void UIHost::Post(ControlHandle owner, std::function<void(Control&)> callback)
    {
        deferred_.push_back({std::move(owner), std::move(callback)});
        ScheduleUpdate();
    }
    void UIHost::Invalidate(bool layout)
    {
        paintDirty_ = true;
        layoutDirty_ |= layout;
        ScheduleUpdate();
    }
    void UIHost::ScheduleUpdate()
    {
        if (!*lifetime_ || updatePending_ || updating_ || !window_.IsCreated() || !window_.IsVisible())
            return;
        updatePending_ = true;
        const auto result = window_.GetPlatformContext().PostTask(
            [weak = std::weak_ptr(lifetime_)]
            {
                if (auto lifetime = weak.lock(); lifetime && *lifetime)
                {
                    auto& host = **lifetime;
                    host.updatePending_ = false;
                    host.Update();
                }
            });
        if (result != LWS::Result::Success)
        {
            updatePending_ = false;
            ReportError("Cannot schedule UI update");
        }
    }
    Size UIHost::MeasureText(std::string_view text, float width, bool wrap, const FontSpec& font)
    {
        return {wrap ? width : canvas_.Measure(text, font), canvas_.TextHeight(text, width, font, wrap)};
    }
    size_t UIHost::HitTestText(std::string_view text, float x, const FontSpec& font)
    {
        return canvas_.HitTestText(text, x, font);
    }
    float UIHost::MeasureCaret(std::string_view text, size_t byteOffset, const FontSpec& font)
    {
        return canvas_.Caret(text, byteOffset, font);
    }
    void UIHost::Update()
    {
        if (updating_ || !window_.IsCreated() || !window_.IsVisible())
            return;
        updating_ = true;
        try
        {
            retired_.clear();
            auto work = std::move(deferred_);
            deferred_.clear();
            for (auto& [owner, callback] : work)
                if (auto* control = owner.Get())
                    callback(*control);
            if (contextMenu_ && !ContextMenuOwnerValid())
                CloseContextMenu();
            if (popup_ && !popupOwner_)
                ClosePopup(EditPhase::Cancel);
            const auto metrics = window_.GetClientAreaMetrics();
            const auto scale = metrics.Scale();
            if (!scale || !metrics.pixels || metrics.pixels->x <= 0 || metrics.pixels->y <= 0)
            {
                updating_ = false;
                return;
            }
            const auto size = metrics.logical;
            const bool drawFrame = paintDirty_ || layoutDirty_;
            if (drawFrame)
                canvas_.Begin(metrics.pixels->x, metrics.pixels->y, *scale);
            // Full-tree layout keeps parent/child dependencies explicit. Finer measure/
            // arrange invalidation needs cache propagation rules; defer until profiling
            // shows layout cost matters for real profiles.
            if (layoutDirty_)
            {
                layoutDirty_ = false;
                if (root_)
                {
                    root_->Measure({float(size.x), float(size.y)});
                    root_->Arrange({0, 0, float(size.x), float(size.y)});
                }
                if (popup_)
                {
                    popup_->Measure({popupBounds_.width, popupBounds_.height});
                    popup_->Arrange(popupBounds_);
                }
                if (contextMenu_)
                {
                    contextMenu_->Measure({contextMenuBounds_.width, contextMenuBounds_.height});
                    contextMenu_->Arrange(contextMenuBounds_);
                }
            }
            if (drawFrame)
            {
                paintDirty_ = false;

                canvas_.Fill(0, 0, float(size.x), float(size.y), theme_.background);
                if (root_)
                    root_->Render(canvas_);
                if (popup_)
                {
                    if (popupModal_)
                        canvas_.Fill(0, 0, float(size.x), float(size.y), popup_->Style().modalOverlay);
                    canvas_.Fill(popupBounds_.x, popupBounds_.y, popupBounds_.width, popupBounds_.height,
                                 popup_->Style().background);
                    popup_->Render(canvas_);
                }
                if (contextMenu_)
                    contextMenu_->Render(canvas_);
                const auto result = window_.PresentBitmap(canvas_.End());
                // Readiness is published through native paint/metrics events. Posting
                // retries here would keep an unconfigured surface's event loop busy.
                if (result == LWS::Result::InvalidState)
                {
                    paintDirty_ = true;
                    updating_ = false;
                    return;
                }
                else if (result != LWS::Result::Success)
                    ReportError("Cannot present UI bitmap");
            }
        }
        catch (const std::exception& e)
        {
            updating_ = false;
            ReportError(e.what());
            return;
        }
        updating_ = false;
        if (paintDirty_ || layoutDirty_ || !deferred_.empty())
            ScheduleUpdate();
    }
    bool UIHost::HasFocusWithin(const Control& control) const
    {
        for (auto* current = focus_.Get(); current; current = current->Parent())
            if (current == &control)
                return true;
        return false;
    }
    bool UIHost::HasPopupWithin(const Control& control) const
    {
        for (auto* current = popupOwner_.Get(); current; current = current->Parent())
            if (current == &control)
                return true;
        return false;
    }
    bool UIHost::HasHoverWithin(const Control& control) const
    {
        for (auto* current = pointerHover_.Get(); current; current = current->Parent())
            if (current == &control)
                return true;
        return false;
    }
    bool UIHost::InPopup(Control* c) const
    {
        for (; c; c = c->Parent())
            if (c == popup_.get())
                return true;
        return false;
    }
    bool UIHost::Focus(Control* c)
    {
        if (c && (c->Host() != this || !Available(c)))
            return false;
        if (c == focus_.Get())
            return true;
        CloseContextMenu();
        if (auto* old = focus_.Get())
        {
            if (!old->Finish(c ? c->focusLossPhase : EditPhase::Commit))
                return false;
            for (auto* current = old; current; current = current->Parent())
                current->Dispatch({InputKind::Blur});
        }
        focus_ = c ? c->Handle() : ControlHandle{};
        if (c)
        {
            c->Dispatch({InputKind::Focus});
            Hover(c);
        }
        Invalidate();
        return true;
    }
    void UIHost::Capture(Control* c)
    {
        capture_ = c->Handle();
#ifdef LWS_HAS_WIN32_BACKEND
        (void) LWS::Win32::SetMouseCapture(window_, true);
#endif
        // Wayland supplies an implicit grab during button drags.
    }
    void UIHost::ReleaseCapture(Control* c)
    {
        if (capture_.Get() != c)
            return;
        capture_ = {};
#ifdef LWS_HAS_WIN32_BACKEND
        (void) LWS::Win32::SetMouseCapture(window_, false);
#endif
    }
    void UIHost::Detached(Control& c)
    {
        if (contextMenuOwner_.Get() == &c)
            CloseContextMenu();
        if (popupOwner_.Get() == &c)
            ClosePopup(EditPhase::Cancel);
        ReleaseCapture(&c);
        if (focus_.Get() == &c)
            focus_ = {};
        if (hover_.Get() == &c)
            hover_ = {};
    }
    bool UIHost::OpenPopup(Control& owner, std::unique_ptr<Control> popup, Rect bounds,
                           std::function<void(EditPhase)> closed, bool modal)
    {
        if (!popup || owner.Host() != this)
            return false;
        const auto ownerHandle = owner.Handle();
        ClosePopup();
        if (popup_ || !ownerHandle || owner.Host() != this)
            return false;
        if (auto* captured = capture_.Get())
        {
            captured->Dispatch({InputKind::Cancel});
            ReleaseCapture(captured);
        }
        // Cancel is another callback boundary: it may retire the owner or open a popup.
        if (popup_ || capture_ || !ownerHandle || owner.Host() != this)
            return false;
        popupModal_ = modal;
        popupOwner_ = owner.Handle();
        popup_ = std::move(popup);
        auto popupTheme = PopupStyle();
        if (!popupStyle_)
            popupTheme.background = theme_.popupSurface;
        popup_->SetStyle(std::move(popupTheme));
        popup_->SetHighlightPattern(std::string(owner.HighlightPattern()));
        popup_->Attach(this, nullptr);
        popupClosed_ = std::move(closed);
        auto size = window_.GetClientAreaMetrics().logical;
        float w = float(size.x), h = float(size.y);
        bounds.width = std::min(bounds.width, w);
        bounds.height = std::min(bounds.height, h);
        bounds.x = std::clamp(bounds.x, 0.f, std::max(0.f, w - bounds.width));
        bounds.y = std::clamp(bounds.y, 0.f, std::max(0.f, h - bounds.height));
        popupBounds_ = bounds;
        Invalidate(true);
        return true;
    }
    void UIHost::ClosePopup(EditPhase phase)
    {
        CloseContextMenu();
        if (!popup_)
            return;
        if (!popup_->Finish(phase))
            return;
        auto owner = popupOwner_;
        auto closed = std::move(popupClosed_);
        popup_->Attach(nullptr, nullptr);
        retired_.push_back(std::move(popup_));
        popupOwner_ = {};
        if (owner)
        {
            Focus(owner.Get());
            if (closed)
                closed(phase);
        }
        Invalidate(true);
    }
    bool UIHost::Finish(EditPhase phase)
    {
        ClosePopup(phase);
        if (popup_)
            return false;
        return !root_ || root_->Finish(phase);
    }
    void UIHost::Hover(Control* c)
    {
        if (pointerHover_.Get() != c)
        {
            if (auto* previous = pointerHover_.Get())
            {
                previous->hovered_ = false;
                previous->Invalidate();
            }
            pointerHover_ = c ? c->Handle() : ControlHandle{};
            if (c)
            {
                c->hovered_ = true;
                c->Invalidate();
            }
        }
        if (popup_)
            c = popupOwner_.Get();
        if (c == hover_.Get())
            return;
        hover_ = c ? c->Handle() : ControlHandle{};
        std::vector<Control*> ancestors;
        for (; c; c = c->Parent())
            ancestors.push_back(c);
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it)
            (*it)->OnHover.Raise();
    }
    bool UIHost::ContextMenuOwnerValid() const
    {
        auto* owner = contextMenuOwner_.Get();
        return Available(owner) && owner->Host() == this && (!popup_ || InPopup(owner));
    }
    bool UIHost::ShowContextMenu(Control& owner, const ContextMenuRequest& request, std::vector<ContextMenuItem> items)
    {
        CloseContextMenu();
        if (!Available(&owner) || owner.Host() != this || (popup_ && !InPopup(&owner)) || capture_)
            return false;
        std::vector<ContextMenuItem> normalized;
        for (auto& item : items)
        {
            if (item.separator && (normalized.empty() || normalized.back().separator))
                continue;
            normalized.push_back(std::move(item));
        }
        if (!normalized.empty() && normalized.back().separator)
            normalized.pop_back();
        if (normalized.empty())
            return false;
        const auto handle = owner.Handle();
        auto menu = std::make_unique<internal::ContextMenu>(
            std::move(normalized),
            [this, handle](std::function<void(Control&)> action)
            {
                CloseContextMenu();
                Post(handle,
                     [action = std::move(action)](Control& control)
                     {
                         if (Available(&control))
                             action(control);
                     });
            },
            request.keyboard);
        auto style = PopupStyle();
        if (!popupStyle_)
            style.background = theme_.popupSurface;
        menu->SetStyle(style);
        menu->SetHighlightPattern("");
        menu->Attach(this, nullptr);
        const auto size = window_.GetClientAreaMetrics().logical;
        const float width = std::min(float(size.x), menu->NaturalWidth(*this));
        const float maxHeight = std::min(
            float(size.y), std::max(1.f, style.menuMaxRows) *
                               std::max(style.menuRowHeight,
                                        MeasureText("Mg", width, false, style.font).height + 2 * style.menuPadding));
        if (width <= 0 || maxHeight <= 0)
        {
            menu->Attach(nullptr, nullptr);
            return false;
        }
        const auto measured = menu->Measure({width, maxHeight});
        contextMenuBounds_ = {std::clamp(request.x, 0.f, std::max(0.f, float(size.x) - width)),
                              std::clamp(request.y, 0.f, std::max(0.f, float(size.y) - measured.height)), width,
                              measured.height};
        menu->Arrange(contextMenuBounds_);
        contextMenuOwner_ = handle;
        contextMenu_ = std::move(menu);
        Invalidate();
        return true;
    }
    void UIHost::CloseContextMenu()
    {
        if (!contextMenu_)
            return;
        contextMenu_->Dispatch({InputKind::Cancel});
        contextMenu_->Finish(EditPhase::Cancel);
        contextMenuOwner_ = {};
        contextMenu_->Attach(nullptr, nullptr);
        retired_.push_back(std::move(contextMenu_));
        Invalidate();
    }
    bool UIHost::RequestContextMenu(Input input, bool keyboard)
    {
        if (capture_)
            return true;
        auto* tree = popup_ ? popup_.get() : root_.get();
        auto* target = keyboard ? focus_.Get() : tree ? tree->HitTest(input.x, input.y) : nullptr;
        if (!Available(target) || (popup_ && !InPopup(target)))
            return false;
        auto* owner = target;
        while (owner && !owner->contextMenuProvider_)
            owner = owner->Parent();
        if (!owner)
            return false;
        auto* focus = target;
        while (focus && !focus->Focusable())
            focus = focus->Parent();
        if (focus && !Focus(focus))
            return true;
        if (keyboard)
        {
            const auto b = target->Bounds();
            input.x = b.x;
            input.y = b.y + b.height;
        }
        ContextMenuRequest request{target->Handle(), input.x, input.y, keyboard};
        // Copy the provider so replacing it from its own callback is safe.
        auto provider = owner->contextMenuProvider_;
        ShowContextMenu(*owner, request, provider(*owner, request));
        return true;
    }
    bool UIHost::RouteContextMenu(const Input& input)
    {
        if (!ContextMenuOwnerValid())
        {
            CloseContextMenu();
            return true;
        }
        if (input.kind == InputKind::Cancel ||
            (input.kind == InputKind::KeyDown && (input.key == LWS::KeyCode::Escape || input.key == LWS::KeyCode::Tab)))
        {
            CloseContextMenu();
            return true;
        }
        const bool pointer = input.kind == InputKind::Move || input.kind == InputKind::Down ||
                             input.kind == InputKind::Up || input.kind == InputKind::Wheel;
        if (input.kind == InputKind::Down && !contextMenuBounds_.Contains(input.x, input.y))
        {
            suppressPointerRelease_ = true;
            CloseContextMenu();
            return true;
        }
        if ((input.kind == InputKind::Down || input.kind == InputKind::Up) && input.button != LWS::MouseButton::Left)
            return true;
        auto* target = pointer ? capture_.Get() : contextMenu_.get();
        if (!target)
            target = contextMenu_->HitTest(input.x, input.y);
        if (!target)
            target = contextMenu_.get();
        for (auto* control = target; control;)
        {
            const auto parent = control->Parent() ? control->Parent()->Handle() : ControlHandle{};
            if (control->Dispatch(input))
                break;
            control = parent.Get();
        }
        return true;
    }
    bool UIHost::Route(Input input)
    {
        if (input.kind == InputKind::Up && suppressPointerRelease_)
        {
            suppressPointerRelease_ = false;
            return true;
        }
        if (input.kind == InputKind::Down)
            suppressPointerRelease_ = false;
        if (contextMenu_)
            return RouteContextMenu(input);
        if ((input.kind == InputKind::Down || input.kind == InputKind::Up) && input.button != LWS::MouseButton::Left)
        {
            if (input.button == LWS::MouseButton::Right)
                return input.kind == InputKind::Down || RequestContextMenu(input, false);
            return false;
        }
        if (input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::F10 && input.shift && !input.control &&
            !input.alt)
            return input.repeat || RequestContextMenu(input, true);
        auto* preview = popup_ ? popup_.get() : root_.get();
        if (preview && preview->PreviewInput(input))
            return true;
        bool pointer = input.kind == InputKind::Down || input.kind == InputKind::Up || input.kind == InputKind::Move ||
                       input.kind == InputKind::Wheel;
        if (input.kind == InputKind::Cancel)
        {
            if (auto* c = capture_.Get())
            {
                c->Dispatch(input);
                ReleaseCapture(c);
            }
            return Finish(EditPhase::Cancel);
        }
        if (input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Escape && popup_)
        {
            ClosePopup(EditPhase::Cancel);
            return true;
        }
        Control* target = nullptr;
        if (pointer)
        {
            target = capture_.Get();
            if (target && popup_ && !InPopup(target))
                target = nullptr;
            if (!target)
                target = popup_  ? popup_->HitTest(input.x, input.y)
                         : root_ ? root_->HitTest(input.x, input.y)
                                 : nullptr;
            Hover(target);
            if (input.kind == InputKind::Down)
            {
                if (popup_ && !target)
                {
                    if (!popupModal_)
                        ClosePopup();
                    return true;
                }
                auto* focus = target;
                while (focus && !focus->Focusable())
                    focus = focus->Parent();
                if (!Focus(focus))
                    return true;
            }
        }
        else
        {
            target = focus_.Get();
            // Logical focus can survive disabling a control; unavailable ancestry cannot receive input.
            if (target && (target->Host() != this || !Available(target)))
                target = nullptr;
            if (popup_ && !InPopup(target))
                target = popup_.get();
            if (input.kind == InputKind::KeyDown && input.key == LWS::KeyCode::Tab)
            {
                // Composite widgets may define a local tab sequence before the default traversal.
                for (auto* control = target; control; control = control->Parent())
                    if (control->Dispatch(input))
                        return true;
                std::vector<Control*> choices;
                auto collect = [&](auto&& self, Control* c) -> void
                {
                    if (!c || !c->Visible() || !c->Enabled())
                        return;
                    if (c->Focusable())
                        choices.push_back(c);
                    if (auto* group = dynamic_cast<Container*>(c))
                        for (auto& child : group->Children())
                            self(self, child.get());
                };
                collect(collect, popup_ ? popup_.get() : root_.get());
                if (!choices.empty())
                {
                    auto it = std::find(choices.begin(), choices.end(), target);
                    int index = it == choices.end() ? -1 : int(it - choices.begin());
                    index = (index + (input.shift ? int(choices.size()) - 1 : 1)) % int(choices.size());
                    Focus(choices[index]);
                }
                return true;
            }
        }
        for (auto* c = target; c;)
        {
            auto parent = c->Parent() ? c->Parent()->Handle() : ControlHandle{};
            if (c->Dispatch(input))
                return true;
            c = parent.Get();
        }
        return popup_ != nullptr;
    }
    bool UIHost::Event(const LWS::AnyEvent& event)
    {
        if (std::holds_alternative<LWS::EventWindowDestroying>(event))
        {
            Finish(EditPhase::Cancel);
            deferred_.clear();
            capture_ = {};
            focus_ = {};
            return false;
        }
        Input input{InputKind::Move};
        if (auto* e = std::get_if<LWS::EventMouseMove>(&event))
        {
            input.x = float(e->position.x);
            input.y = float(e->position.y);
        }
        else if (auto* e = std::get_if<LWS::EventMouseButton>(&event))
        {
            input.button = e->button;
            input.kind = e->pressed ? InputKind::Down : InputKind::Up;
            input.x = float(e->position.x);
            input.y = float(e->position.y);
        }
        else if (auto* e = std::get_if<LWS::EventMouseWheel>(&event))
        {
            input.kind = InputKind::Wheel;
            input.wheel = float(e->steps());
            input.x = float(e->position.x);
            input.y = float(e->position.y);
        }
        else if (auto* e = std::get_if<LWS::EventKeyDown>(&event))
        {
            input.kind = InputKind::KeyDown;
            input.key = e->key;
            input.repeat = e->repeat;
        }
        else if (auto* e = std::get_if<LWS::EventKeyUp>(&event))
        {
            input.kind = InputKind::KeyUp;
            input.key = e->key;
        }
        else if (auto* e = std::get_if<LWS::EventTextInput>(&event))
        {
            input.kind = InputKind::Text;
            input.text = e->text;
        }
        else if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event))
        {
            ClosePopup(EditPhase::Cancel);
            Invalidate(true);
            return false;
        }
        else if (std::holds_alternative<LWS::EventPaint>(event))
        {
            Invalidate();
            return false;
        }
        else if (std::holds_alternative<LWS::EventMouseCaptureLost>(event))
        {
            if (auto* captured = capture_.Get())
            {
                capture_ = {};
                captured->Dispatch({InputKind::Cancel});
            }
            return false;
        }
        else if (std::holds_alternative<LWS::EventFocusGained>(event))
        {
            if (auto* focused = focus_.Get())
                focused->Dispatch({InputKind::Focus});
            Invalidate();
            return false;
        }
        else if (std::holds_alternative<LWS::EventFocusLost>(event))
        {
            CloseContextMenu();
            if (auto* captured = capture_.Get())
            {
                captured->Dispatch({InputKind::Cancel});
                ReleaseCapture(captured);
            }
            for (auto* current = focus_.Get(); current; current = current->Parent())
                current->Dispatch({InputKind::Blur});
            return false;
        }
        else if (std::holds_alternative<LWS::EventMouseLeave>(event))
        {
            Hover(nullptr);
            return false;
        }
        else
            return false;
        input.control = window_.GetPlatformContext().IsKeyPressed(LWS::KeyCode::Control).value_or(false);
        input.shift = window_.GetPlatformContext().IsKeyPressed(LWS::KeyCode::Shift).value_or(false);
        input.alt = window_.GetPlatformContext().IsKeyPressed(LWS::KeyCode::Alt).value_or(false);
        return Route(input);
    }
}  // namespace LWSUI
