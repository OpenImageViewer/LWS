#include <LWSUI/UIHost.hpp>
#include "MenuHost.hpp"
#include <algorithm>
#include <cmath>
namespace LWSUI
{
    namespace
    {
        constexpr size_t kNoIndex = static_cast<size_t>(-1);
        struct Mnemonic
        {
            std::string label;
            size_t index = kNoIndex;
        };
        /// Splits a label into its display text and the underlined mnemonic position.
        Mnemonic ParseMnemonic(std::string_view text)
        {
            Mnemonic result;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i] == '&' && i + 1 < text.size())
                {
                    const char next = text[++i];
                    if (next == '&')
                    {
                        result.label += '&';
                        continue;
                    }
                    if (result.index == kNoIndex)
                        result.index = result.label.size();
                    result.label += next;
                    continue;
                }
                result.label += text[i];
            }
            return result;
        }
        char Upper(char c)
        {
            return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
        }
        bool MnemonicMatches(const Mnemonic& mnemonic, char key)
        {
            return mnemonic.index != kNoIndex && mnemonic.index < mnemonic.label.size() &&
                   Upper(mnemonic.label[mnemonic.index]) == Upper(key);
        }
        int32_t Round(float value)        {
            return static_cast<int32_t>(std::lround(value));
        }
        bool PointerKind(InputKind kind)
        {
            return kind == InputKind::Move || kind == InputKind::Down || kind == InputKind::Up ||
                   kind == InputKind::Wheel;
        }
    }  // namespace
    MenuItem MenuItem::Separator()
    {
        MenuItem item;
        item.separator = true;
        return item;
    }
    void MenuBar::SetItems(std::vector<MenuBarItem> items)
    {
        if (auto* host = Host())
            host->CloseMainMenu();
        items_ = std::move(items);
        Invalidate(true);
    }
    float MenuBar::TextWidth(std::string_view text) const
    {
        if (auto* host = Host())
            return host->MeasureText(text, 10000, false, Font()).width;
        return Font().size * 0.6f * float(text.size());
    }
    float MenuBar::TextHeight(std::string_view text) const
    {
        if (auto* host = Host())
            return host->MeasureText(text, 10000, false, Font()).height;
        return Font().size * 1.4f;
    }
    float MenuBar::ItemExtent(size_t index) const
    {
        if (orientation == Orientation::Vertical)
            return Style().menuBarHeight;
        return TextWidth(ParseMnemonic(items_[index].label).label) + 2 * Style().menuBarPadding;
    }
    float MenuBar::NaturalSpan() const
    {
        float span = 0;
        for (size_t index = 0; index < items_.size(); ++index)
            span += ItemExtent(index);
        return span;
    }
    float MenuBar::Thickness() const
    {
        if (orientation == Orientation::Horizontal)
            return Style().menuBarHeight;
        float width = 0;
        for (const auto& item : items_)
            width = std::max(width, TextWidth(ParseMnemonic(item.label).label) + 2 * Style().menuBarPadding);
        return width;
    }
    Size MenuBar::OnMeasure(Size available)
    {
        const bool horizontal = orientation == Orientation::Horizontal;
        const float limit = std::max(0.f, horizontal ? available.width : available.height);
        float span = stretch ? limit : NaturalSpan();
        if (span_ > 0)
            span = span_;
        if (minSpan_ > 0)
            span = std::max(span, minSpan_);
        if (maxSpan_ > 0)
            span = std::min(span, maxSpan_);
        span = std::clamp(span, 0.f, limit);
        return horizontal ? Size{span, Thickness()} : Size{Thickness(), span};
    }
    std::vector<Rect> MenuBar::ItemRects() const
    {
        const bool horizontal = orientation == Orientation::Horizontal;
        const auto bounds = Bounds();
        std::vector<Rect> rects;
        rects.reserve(items_.size());
        float position = horizontal ? bounds.x : bounds.y;
        for (size_t index = 0; index < items_.size(); ++index)
        {
            const float extent = ItemExtent(index);
            rects.push_back(horizontal ? Rect{position, bounds.y, extent, bounds.height}
                                       : Rect{bounds.x, position, bounds.width, extent});
            position += extent;
        }
        return rects;
    }
    Rect MenuBar::ItemRect(size_t index) const
    {
        const auto rects = ItemRects();
        return index < rects.size() ? rects[index] : Rect{};
    }
    size_t MenuBar::HitItem(float x, float y) const
    {
        if (!Bounds().Contains(x, y))
            return NoItem;
        const auto rects = ItemRects();
        for (size_t index = 0; index < rects.size(); ++index)
            if (rects[index].Contains(x, y))
                return index;
        return NoItem;
    }
    void MenuBar::OnRender(Canvas& canvas)
    {
        const auto bounds = Bounds();
        const auto& style = Style();
        canvas.Fill(bounds.x, bounds.y, bounds.width, bounds.height, style.surface);
        if (floating)
        {
            canvas.Line(bounds.x, bounds.y, bounds.x + bounds.width, bounds.y, style.borderWidth, style.line);
            canvas.Line(bounds.x, bounds.y + bounds.height, bounds.x + bounds.width, bounds.y + bounds.height,
                        style.borderWidth, style.line);
            canvas.Line(bounds.x, bounds.y, bounds.x, bounds.y + bounds.height, style.borderWidth, style.line);
            canvas.Line(bounds.x + bounds.width, bounds.y, bounds.x + bounds.width, bounds.y + bounds.height,
                        style.borderWidth, style.line);
        }
        const auto rects = ItemRects();
        for (size_t index = 0; index < items_.size() && index < rects.size(); ++index)
        {
            const auto& item = items_[index];
            const auto& rect = rects[index];
            const bool highlighted = int(index) == hot;
            if (highlighted)
                canvas.Fill(rect.x, rect.y, rect.width, rect.height, style.selection);
            const auto mnemonic = ParseMnemonic(item.label);
            const auto color = !item.enabled  ? style.muted
                               : highlighted ? style.selectionForeground
                                             : style.foreground;
            const float width = TextWidth(mnemonic.label);
            const float height = TextHeight(mnemonic.label);
            const float x = rect.x + (rect.width - width) / 2;
            const float y = rect.y + (rect.height - height) / 2;
            DrawText(canvas, mnemonic.label, x, y, std::max(1.f, width), height, color, false, false);
            if (mnemonic.index != kNoIndex)
            {
                const float start = x + TextWidth(mnemonic.label.substr(0, mnemonic.index));
                const float end = start + TextWidth(mnemonic.label.substr(mnemonic.index, 1));
                canvas.Line(start, y + height - style.borderWidth, end, y + height - style.borderWidth,
                            style.borderWidth, color);
            }
        }
    }
    bool MenuBar::OnInput(const Input&)
    {
        // Interaction is owned by the menu session; the bar only renders state set through `hot`.
        return false;
    }
    namespace internal
    {
        std::optional<Input> InputFromEvent(const LWS::AnyEvent& event, LWS::PlatformContext& platform)
        {
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
            else
                return std::nullopt;
            input.control = platform.IsKeyPressed(LWS::KeyCode::Control).value_or(false);
            input.shift = platform.IsKeyPressed(LWS::KeyCode::Shift).value_or(false);
            input.alt = platform.IsKeyPressed(LWS::KeyCode::Alt).value_or(false);
            return input;
        }
        Size DropdownPanel::Row::OnMeasure(Size available)
        {
            const auto& style = Style();
            if (item_.separator)
                return {available.width, 2 * style.menuPadding + style.borderWidth};
            const float height = Host()->MeasureText(item_.label, 10000, false, Font()).height;
            return {available.width, std::max(style.menuRowHeight, height + 2 * style.menuPadding)};
        }
        float DropdownPanel::Row::CheckWidth() const
        {
            return checks_ ? Font().size + Style().menuPadding : 0;
        }
        float DropdownPanel::Row::ArrowWidth() const
        {
            return arrows_ ? Style().menuArrowWidth : 0;
        }
        float DropdownPanel::Row::HintWidth(float width) const
        {
            return item_.shortcut.empty()
                       ? 0
                       : std::min(std::max(0.f, width * .4f),
                                  Host()->MeasureText(item_.shortcut, 10000, false, Font()).width);
        }
        void DropdownPanel::Row::OnRender(Canvas& canvas)
        {
            const auto bounds = Bounds();
            const auto& style = Style();
            const float padding = style.menuPadding;
            if (item_.separator)
            {
                canvas.Fill(bounds.x + padding, bounds.y + bounds.height / 2,
                            std::max(0.f, bounds.width - 2 * padding), style.borderWidth, style.line);
                return;
            }
            if (active)
                canvas.Fill(bounds.x, bounds.y, bounds.width, bounds.height, style.selection);
            const bool usable = item_.enabled;
            const auto color = usable ? style.foreground : style.muted;
            const float check = CheckWidth(), arrow = ArrowWidth();
            if (item_.checked.value_or(false))
            {
                const float x = bounds.x + padding, y = bounds.y + bounds.height / 2, unit = Font().size;
                canvas.Line(x, y, x + unit * .3f, y + unit * .25f, style.borderWidth, color);
                canvas.Line(x + unit * .3f, y + unit * .25f, x + unit * .75f, y - unit * .3f, style.borderWidth, color);
            }
            const float hint = HintWidth(bounds.width);
            const float labelWidth =
                std::max(1.f, bounds.width - 2 * padding - check - arrow - (hint > 0 ? hint + padding : 0));
            const auto mnemonic = ParseMnemonic(item_.label);
            const float height = std::min(bounds.height, Host()->MeasureText(mnemonic.label, labelWidth, false, Font()).height);
            const float y = bounds.y + (bounds.height - height) / 2;
            const float x = bounds.x + padding + check;
            DrawText(canvas, mnemonic.label, x, y, labelWidth, height, color, false, false);
            if (mnemonic.index != kNoIndex)
            {
                const float start = x + Host()->MeasureText(mnemonic.label.substr(0, mnemonic.index), 10000, false, Font()).width;
                const float end = start + Host()->MeasureText(mnemonic.label.substr(mnemonic.index, 1), 10000, false, Font()).width;
                canvas.Line(start, y + height - style.borderWidth, end, y + height - style.borderWidth,
                            style.borderWidth, color);
            }
            if (hint > 0)
                canvas.Text(item_.shortcut, bounds.x + bounds.width - padding - arrow - hint, y, hint, height,
                            style.muted, Font());
            if (!item_.submenu.empty())
            {
                const float left = bounds.x + bounds.width - padding - arrow;
                const float cx = left + arrow * .35f, cy = bounds.y + bounds.height / 2;
                const float unit = std::max(2.f, Font().size * .3f);
                canvas.Line(cx, cy - unit, cx + unit * .6f, cy, style.borderWidth, color);
                canvas.Line(cx + unit * .6f, cy, cx, cy + unit, style.borderWidth, color);
            }
        }
        DropdownPanel::DropdownPanel(std::vector<MenuItem> items) : items_(std::move(items))
        {
            auto list = std::make_unique<StackPanel>();
            list->spacing = 0;
            checks_ = std::ranges::any_of(items_, [](const auto& item) { return item.checked.has_value(); });
            arrows_ = std::ranges::any_of(items_, [](const auto& item) { return !item.submenu.empty(); });
            for (const auto& item : items_)
                rows_.push_back(&list->Emplace<Row>(item, checks_, arrows_));
            SetContent(std::move(list));
        }
        float DropdownPanel::NaturalWidth(UIHost& host) const
        {
            const auto& style = Style();
            float label = 0, hint = 0;
            for (const auto& item : items_)
            {
                label = std::max(label, host.MeasureText(ParseMnemonic(item.label).label, 10000, false, Font()).width);
                hint = std::max(hint, host.MeasureText(item.shortcut, 10000, false, Font()).width);
            }
            return label + hint + 4 * style.menuPadding + (checks_ ? Font().size + style.menuPadding : 0) +
                   (arrows_ ? style.menuArrowWidth : 0) + style.scrollbarWidth + style.scrollbarMargin;
        }
        bool DropdownPanel::Selectable(size_t index) const
        {
            const auto& item = items_[index];
            return !item.separator && item.enabled && (bool(item.action) || !item.submenu.empty() || item.checked.has_value());
        }
        std::optional<size_t> DropdownPanel::First() const
        {
            return Next(items_.size() - 1, 1);
        }
        std::optional<size_t> DropdownPanel::Last() const
        {
            return Next(0, -1);
        }
        std::optional<size_t> DropdownPanel::Next(size_t from, int step) const
        {
            if (items_.empty())
                return std::nullopt;
            size_t index = from;
            for (size_t n = 0; n < items_.size(); ++n)
            {
                index = (index + items_.size() + (step > 0 ? 1 : items_.size() - 1)) % items_.size();
                if (Selectable(index))
                    return index;
            }
            return std::nullopt;
        }
        std::optional<size_t> DropdownPanel::Mnemonic(char key, size_t from) const
        {
            if (items_.empty())
                return std::nullopt;
            size_t index = from;
            for (size_t n = 0; n < items_.size(); ++n)
            {
                index = (index + 1) % items_.size();
                if (Selectable(index) && MnemonicMatches(ParseMnemonic(items_[index].label), key))
                    return index;
            }
            return std::nullopt;
        }
        void DropdownPanel::Select(std::optional<size_t> index, bool reveal)
        {
            if (active_ != index)
            {
                if (active_)
                    rows_[*active_]->active = false;
                active_ = index;
                if (active_)
                    rows_[*active_]->active = true;
                Invalidate();
                OnSelect.Raise(active_);
            }
            if (reveal && active_ && Bounds().height > 0)
            {
                const auto row = rows_[*active_]->Bounds();
                if (row.y < Bounds().y)
                    SetOffset(Offset() + row.y - Bounds().y);
                else if (row.y + row.height > Bounds().y + Bounds().height)
                    SetOffset(Offset() + row.y + row.height - Bounds().y - Bounds().height);
            }
        }
        void DropdownPanel::OnRender(Canvas& canvas)
        {
            const auto bounds = Bounds();
            canvas.Fill(bounds.x, bounds.y, bounds.width, bounds.height, Style().background);
            ScrollView::OnRender(canvas);
        }
        bool DropdownPanel::OnInput(const Input& input)
        {
            using K = LWS::KeyCode;
            if (input.kind == InputKind::KeyDown)
            {
                if (input.key == K::Home)
                    Select(First());
                else if (input.key == K::End)
                    Select(Last());
                else if (input.key == K::Up || input.key == K::Down)
                    Select(Next(active_.value_or(input.key == K::Up ? 0 : items_.size() - 1),
                                input.key == K::Up ? -1 : 1));
                else if ((input.key == K::Enter || input.key == K::Space) && !input.repeat && active_ &&
                         Selectable(*active_))
                    OnActivate.Raise(*active_);
                return true;
            }
            if (PointerKind(input.kind))
            {
                std::optional<size_t> hit;
                if (Bounds().Contains(input.x, input.y))
                    for (size_t index = 0; index < rows_.size(); ++index)
                        if (Selectable(index) && rows_[index]->Bounds().Contains(input.x, input.y))
                        {
                            hit = index;
                            break;
                        }
                Select(hit, false);
                // Only the left button participates in press/activate; other buttons leave
                // activation to the host's own routing.
                if (input.button != LWS::MouseButton::Left)
                    return true;
                if (input.kind == InputKind::Down)
                    pressed_ = hit;
                if (input.kind == InputKind::Up)
                {
                    const bool activate = hit && hit == pressed_;
                    pressed_.reset();
                    if (activate)
                        OnActivate.Raise(*hit);
                }
                return true;
            }
            return ScrollView::OnInput(input);
        }
        std::optional<MenuSession::PanelBuild> MenuSession::BuildPanel(UIHost& host, const std::vector<MenuItem>& items)
        {
            const auto metrics = host.Window().GetClientAreaMetrics();
            const float availableWidth = std::max(1.f, float(metrics.logical.x));
            const float availableHeight = std::max(1.f, float(metrics.logical.y));
            auto panel = std::make_unique<DropdownPanel>(items);
            if (panel->Count() == 0)
                return std::nullopt;
            // Rows measure text through the host, so the panel is attached before any Measure.
            host.AttachPanel(*panel);
            const auto& style = host.PopupStyle();
            panel->SetStyle(host.MenuPanelStyle());
            const float width = std::min(panel->NaturalWidth(host), availableWidth);
            const float rowHeight =
                std::max(style.menuRowHeight,
                         host.MeasureText("Mg", width, false, style.font).height + 2 * style.menuPadding);
            const float maxHeight =
                std::min(availableHeight, std::max(1.f, style.menuMaxRows) * std::max(1.f, rowHeight));
            const Size measured = panel->Measure({width, maxHeight});
            if (measured.height <= 0)
            {
                host.DetachPanel(*panel);
                return std::nullopt;
            }
            return PanelBuild{std::move(panel), width, std::min(measured.height, availableHeight)};
        }
        MenuSession& MenuSessionAccess::Get(UIHost& host)
        {
            return *host.menuSession_;
        }
        MenuSession::MenuSession(UIHost& host) : host_(host) {}
        MenuSession::~MenuSession()
        {
            Close();
        }
        void MenuSession::SetHot(int index)
        {
            hot_ = index;
            auto* bar = host_.MainMenu();
            if (bar && bar->hot != index)
            {
                bar->hot = index;
                bar->Invalidate();
            }
        }
        bool MenuSession::CanReuseWindows() const
        {
            return host_.Window().GetPlatformContext().GetBackendId() == LWS::BackendId::Win32;
        }
        std::unique_ptr<LWS::Window> MenuSession::AcquireWindow(LWS::Window* parent)
        {
            while (!pool_.empty())
            {
                auto window = std::move(pool_.back());
                pool_.pop_back();
                // Only windows still owned by the expected parent are reusable; a destroyed native
                // lifetime drops out here rather than being restated.
                if (window->IsCreated() && window->GetParent() == parent)
                    return window;
            }
            return std::make_unique<LWS::Window>(host_.Window().GetPlatformContext());
        }
        void MenuSession::ReleaseWindow(std::unique_ptr<LWS::Window> window)
        {
            if (!window)
                return;
            // The Win32 pool survives the session; Wayland popups cannot be re-shown once dismissed,
            // so anything else retires to the next update, after any in-flight dispatch returns.
            if (CanReuseWindows() && window->IsCreated() && window->GetParent() == &host_.Window())
            {
                std::ignore = window->SetVisible(false);
                pool_.push_back(std::move(window));
                return;
            }
            // Destruction is deferred, but a dismissed popup must leave the screen now: an owner
            // regaining focus or visibility would otherwise keep republishing the stale surface.
            if (window->IsCreated())
                std::ignore = window->SetVisible(false);
            retiredWindows_.push_back(std::move(window));
        }
        void MenuSession::TrimPool()
        {
            for (auto& window : pool_)
                retiredWindows_.push_back(std::move(window));
            pool_.clear();
        }
        void MenuSession::CancelHover()
        {
            if (hoverTimer_)
                hoverTimer_->Enable(false);
            pendingSubmenu_.reset();
        }
        std::optional<size_t> MenuSession::FindLevel(LWS::Window* window) const
        {
            for (size_t index = 0; index < levels_.size(); ++index)
                if (levels_[index]->window.get() == window)
                    return index;
            return std::nullopt;
        }
        size_t MenuSession::IndexOf(const Level* level) const
        {
            for (size_t index = 0; index < levels_.size(); ++index)
                if (levels_[index].get() == level)
                    return index;
            return NoLevel;
        }
        DropdownPanel* MenuSession::Panel(size_t level) const
        {
            return level < levels_.size() ? levels_[level]->panel.get() : nullptr;
        }
        LWS::Window* MenuSession::LevelWindow(size_t level) const
        {
            return level < levels_.size() ? levels_[level]->window.get() : nullptr;
        }
        Rect MenuSession::TopMenuAnchor(size_t barIndex) const
        {
            auto* bar = host_.MainMenu();
            const Rect item = bar->ItemRect(barIndex);
            switch (host_.MainMenuDock())
            {
                case MenuDock::Bottom:
                    return Rect{float(Round(item.x)), float(Round(item.y)), 0, 0};
                case MenuDock::Left:
                    return Rect{float(Round(item.x + item.width)), float(Round(item.y)), 0, 0};
                case MenuDock::Right:
                    return Rect{float(Round(item.x)), float(Round(item.y)), 0, 0};
                case MenuDock::Top:
                    return Rect{float(Round(item.x)), float(Round(item.y + item.height)), 0, 0};
                default:
                    break;
            }
            if (bar->orientation == Orientation::Vertical)
                return Rect{float(Round(item.x + item.width)), float(Round(item.y)), 0, 0};
            return Rect{float(Round(item.x)), float(Round(item.y + item.height)), 0, 0};
        }
        LWS::PopupGravity MenuSession::TopMenuGravity() const
        {
            switch (host_.MainMenuDock())
            {
                case MenuDock::Bottom:
                    return LWS::PopupGravity::UpRight;
                case MenuDock::Right:
                    return LWS::PopupGravity::DownLeft;
                default:
                    return LWS::PopupGravity::DownRight;
            }
        }
        bool MenuSession::CreateLevel(size_t parentLevel, size_t row, std::vector<MenuItem> items, LWS::Point anchor,
                                      LWS::PopupGravity gravity, bool first)
        {
            LWS::Window* parentWindow = parentLevel == NoLevel ? &host_.Window() : LevelWindow(parentLevel);
            if (parentWindow == nullptr || !parentWindow->IsCreated())
                return false;
            auto build = BuildPanel(host_, items);
            if (!build)
                return false;
            auto window = AcquireWindow(parentWindow);
            const bool reused = window->IsCreated();
            if (reused)
            {
                // A pooled Win32 popup keeps its native window: restate placement and show.
                const auto placement = window->RequestPlacement(
                    LWS::WindowPlacementRequest{.position = anchor,
                                                .clientSize = LWS::LogicalSize{Round(build->width),
                                                                               Round(build->height)}});
                if (placement != LWS::Result::Success)
                {
                    ReleaseWindow(std::move(window));
                    return false;
                }
                std::ignore = window->SetVisible(true);
            }
            else
            {
                LWS::WindowConfig config;
                config.parent = parentWindow;
                config.title = NativeText("Menu");
                config.backgroundColor = host_.MenuPanelStyle().background;
                config.visible = true;
                config.popupPlacement = LWS::PopupPlacement{anchor, gravity, LWS::Point{},
                                                            LWS::LogicalSize{Round(build->width), Round(build->height)}, true};
                if (window->Create(config) != LWS::Result::Success)
                {
                    ReleaseWindow(std::move(window));
                    return false;
                }
            }
            auto level = std::make_unique<Level>();
            level->window = std::move(window);
            level->panel = std::move(build->panel);
            level->logical = {0, 0, build->width, build->height};
            host_.AttachPanel(*level->panel);
            level->panel->Measure({build->width, build->height});
            level->panel->Arrange({0, 0, build->width, build->height});
            first ? level->panel->Select(level->panel->First()) : level->panel->Select(level->panel->Last());
            LWS::Window* windowHandle = level->window.get();
            auto listener = level->window->Listen(
                [this, windowHandle](const LWS::AnyEvent& event)
                {
                    return OnLevelEvent(windowHandle, event) ? LWS::EventResponse::Handled
                                                             : LWS::EventResponse::Unhandled;
                });
            if (!listener)
            {
                host_.DetachPanel(*level->panel);
                ReleaseWindow(std::move(level->window));
                return false;
            }
            level->listener = std::move(*listener);
            Level* raw = level.get();
            level->selection = level->panel->OnSelect.Connect([this, raw](std::optional<size_t> activeRow)
                                                              { OnRowSelected(raw, activeRow); });
            level->activation = level->panel->OnActivate.Connect([this, raw](size_t row) { OnRowActivated(raw, row); });
            levels_.push_back(std::move(level));
            host_.Invalidate();
            return true;
        }
        void MenuSession::OpenTopMenu(size_t barIndex, bool first)
        {
            auto* bar = host_.MainMenu();
            if (!bar || barIndex >= bar->Items().size())
                return;
            const MenuBarItem& item = bar->Items()[barIndex];
            if (!item.enabled || item.items.empty())
                return;
            Activate();
            SetHot(int(barIndex));
            orientationAtOpen_ = bar->orientation;
            if (!ReplaceTopMenu(barIndex, first))
            {
                CloseDeepLevels(0);
                const Rect anchor = TopMenuAnchor(barIndex);
                CreateLevel(NoLevel, barIndex, item.items,
                            LWS::Point{Round(anchor.x), Round(anchor.y)}, TopMenuGravity(), first);
            }
        }
        bool MenuSession::ReplaceTopMenu(size_t barIndex, bool first)
        {
            auto* bar = host_.MainMenu();
            if (!bar || levels_.size() != 1)
                return false;
            const MenuBarItem& item = bar->Items()[barIndex];
            if (!item.enabled || item.items.empty())
                return false;
            Level& level = *levels_[0];
            LWS::Window* parentWindow = level.window ? level.window->GetParent() : nullptr;
            if (parentWindow == nullptr || !level.window->IsCreated())
                return false;
            auto build = BuildPanel(host_, item.items);
            if (!build)
                return false;
            const Rect anchor = TopMenuAnchor(barIndex);
            const auto placement = level.window->RequestPlacement(
                LWS::WindowPlacementRequest{.position = LWS::Point{Round(anchor.x), Round(anchor.y)},
                                            .clientSize = LWS::LogicalSize{Round(build->width), Round(build->height)}});
            if (placement != LWS::Result::Success)
                return false;
            CloseDeepLevels(1);
            CancelHover();
            level.selection = {};
            level.activation = {};
            host_.DetachPanel(*level.panel);
            level.panel = std::move(build->panel);
            level.logical = {0, 0, build->width, build->height};
            host_.AttachPanel(*level.panel);
            level.panel->Measure({build->width, build->height});
            level.panel->Arrange({0, 0, build->width, build->height});
            first ? level.panel->Select(level.panel->First()) : level.panel->Select(level.panel->Last());
            Level* raw = &level;
            level.selection = level.panel->OnSelect.Connect([this, raw](std::optional<size_t> activeRow)
                                                            { OnRowSelected(raw, activeRow); });
            level.activation = level.panel->OnActivate.Connect([this, raw](size_t row) { OnRowActivated(raw, row); });
            level.dirty = true;
            SetHot(int(barIndex));
            host_.Invalidate();
            return true;
        }
        void MenuSession::SwitchTopMenuTo(size_t barIndex)
        {
            OpenTopMenu(barIndex, true);
        }
        void MenuSession::SwitchTopMenu(int step)
        {
            auto* bar = host_.MainMenu();
            if (!bar || bar->Items().empty())
            {
                Close();
                return;
            }
            const size_t count = bar->Items().size();
            size_t index = hot_ < 0 ? (step > 0 ? count - 1 : 0) : size_t(hot_);
            for (size_t n = 0; n < count; ++n)
            {
                index = (index + count + (step > 0 ? 1 : count - 1)) % count;
                if (bar->Items()[index].enabled && !bar->Items()[index].items.empty())
                    break;
            }
            OpenTopMenu(index, true);
        }
        void MenuSession::MoveHot(int step)
        {
            auto* bar = host_.MainMenu();
            if (!bar || bar->Items().empty())
                return;
            const size_t count = bar->Items().size();
            size_t index = hot_ < 0 ? (step > 0 ? count - 1 : 0) : size_t(hot_);
            for (size_t n = 0; n < count; ++n)
            {
                index = (index + count + (step > 0 ? 1 : count - 1)) % count;
                if (bar->Items()[index].enabled && !bar->Items()[index].items.empty())
                {
                    SetHot(int(index));
                    return;
                }
            }
        }
        void MenuSession::OpenSubmenu(size_t level, size_t row)
        {
            if (level >= levels_.size())
                return;
            Level& parent = *levels_[level];
            if (row >= parent.panel->Count())
                return;
            const MenuItem& item = parent.panel->Item(row);
            if (item.submenu.empty() || !parent.panel->Selectable(row))
                return;
            // A keyboard descent must not leave the row's hover timer armed behind it.
            CancelHover();
            parent.panel->Select(row);
            CloseDeepLevels(level + 1);
            const Rect anchor = parent.panel->RowRect(row);
            CreateLevel(level, row, item.submenu, LWS::Point{Round(anchor.x + anchor.width), Round(anchor.y)},
                        LWS::PopupGravity::DownRight, true);
        }
        void MenuSession::ActivateRow(size_t level, size_t row)
        {
            if (level >= levels_.size())
                return;
            DropdownPanel* panel = levels_[level]->panel.get();
            if (row >= panel->Count() || !panel->Selectable(row))
                return;
            const MenuItem& item = panel->Item(row);
            if (!item.submenu.empty())
            {
                OpenSubmenu(level, row);
                return;
            }
            auto action = item.action;
            auto* bar = host_.MainMenu();
            if (!bar)
            {
                Close();
                return;
            }
            const auto handle = bar->Handle();
            Close();
            if (action)
                host_.Post(handle, [action = std::move(action)](Control&) { action(); });
        }
        void MenuSession::OnRowSelected(Level* level, std::optional<size_t> row)
        {
            const size_t index = IndexOf(level);
            if (index == NoLevel || !row)
                return;
            const MenuItem& item = level->panel->Item(*row);
            if (!item.submenu.empty() && level->panel->Selectable(*row))
                ArmHover(index, *row);
            else
            {
                CancelHover();
                CloseDeepLevels(index + 1);
            }
        }
        void MenuSession::OnRowActivated(Level* level, size_t row)
        {
            const size_t index = IndexOf(level);
            if (index != NoLevel)
                ActivateRow(index, row);
        }
        void MenuSession::ArmHover(size_t level, size_t row)
        {
            CancelHover();
            const int delay = host_.Style().menuHoverDelayMs;
            if (delay <= 0)
            {
                OpenSubmenu(level, row);
                return;
            }
            pendingSubmenu_ = std::pair{level, row};
            if (!hoverTimer_)
            {
                hoverTimer_ = std::make_unique<LWS::HighPrecisionTimer>(
                    host_.Window().GetPlatformContext(),
                    [this]
                    {
                        const auto pending = pendingSubmenu_;
                        pendingSubmenu_.reset();
                        if (pending)
                            OpenSubmenu(pending->first, pending->second);
                    });
            }
            hoverTimer_->SetDueTime(uint32_t(delay));
            hoverTimer_->SetRepeatInterval(0);
            hoverTimer_->Enable(true);
        }
        void MenuSession::CloseLevel(size_t level)
        {
            if (level >= levels_.size())
                return;
            auto entry = std::move(levels_[level]);
            levels_.erase(levels_.begin() + level);
            CancelHover();
            entry->selection = {};
            entry->activation = {};
            entry->listener = {};
            if (entry->panel)
                host_.DetachPanel(*entry->panel);
            ReleaseWindow(std::move(entry->window));
            retired_.push_back(std::move(entry));
            auto* bar = host_.MainMenu();
            if (bar)
                bar->Invalidate();
        }
        void MenuSession::CloseDeepLevels(size_t keep)
        {
            while (levels_.size() > keep)
                CloseLevel(levels_.size() - 1);
        }
        void MenuSession::Close()
        {
            CancelHover();
            active_ = false;
            menuUsed_ = false;
            CloseDeepLevels(0);
            TrimPool();
            SetHot(-1);
            host_.Invalidate();
        }
        void MenuSession::Activate()
        {
            auto* bar = host_.MainMenu();
            if (!bar || bar->Items().empty())
                return;
            active_ = true;
            menuUsed_ = false;
            orientationAtOpen_ = bar->orientation;
            for (size_t index = 0; index < bar->Items().size(); ++index)
                if (bar->Items()[index].enabled && !bar->Items()[index].items.empty())
                {
                    SetHot(int(index));
                    break;
                }
            host_.Invalidate();
        }
        bool MenuSession::Mnemonic(char key)
        {
            for (size_t level = levels_.size(); level-- > 0;)
            {
                auto* panel = levels_[level]->panel.get();
                const auto row = panel->Mnemonic(key, panel->Active().value_or(kNoIndex));
                if (row)
                {
                    panel->Select(*row);
                    return true;
                }
            }
            auto* bar = host_.MainMenu();
            if (bar)
            {
                const size_t count = bar->Items().size();
                const size_t start = hot_ < 0 ? count - 1 : size_t(hot_);
                for (size_t n = 0; n < count; ++n)
                {
                    const size_t index = (start + 1 + n) % count;
                    const auto& item = bar->Items()[index];
                    if (item.enabled && !item.items.empty() && MnemonicMatches(ParseMnemonic(item.label), key))
                    {
                        OpenTopMenu(index, true);
                        return true;
                    }
                }
            }
            return active_;
        }
        bool MenuSession::KeyDown(const Input& input)
        {
            using K = LWS::KeyCode;
            if ((input.key == K::Alt || input.key == K::F10) && input.repeat)
                return active_;
            if (input.key == K::Alt && !input.control && !input.shift)
            {
                active_ ? Close() : Activate();
                return true;
            }
            if (input.key == K::F10 && !input.shift && !input.control && !input.alt)
            {
                active_ ? Close() : Activate();
                return true;
            }
            if (!active_)
                return false;
            // Mnemonics are driven from text input only: the platforms deliver a character event
            // for the same key press, and handling both would advance the match twice.
            menuUsed_ = true;
            auto* bar = host_.MainMenu();
            const bool horizontal = bar == nullptr || bar->orientation == Orientation::Horizontal;
            if (input.key == K::Escape)
            {
                levels_.size() > 1 ? CloseLevel(levels_.size() - 1) : Close();
                return true;
            }
            if (input.key == K::Tab)
            {
                Close();
                return true;
            }
            if (levels_.empty())
            {
                if (horizontal ? (input.key == K::Left || input.key == K::Right)
                               : (input.key == K::Up || input.key == K::Down))
                    MoveHot(input.key == K::Right || input.key == K::Down ? 1 : -1);
                else if (horizontal ? (input.key == K::Up || input.key == K::Down)
                                    : (input.key == K::Left || input.key == K::Right))
                {
                    const bool backward = input.key == K::Up || input.key == K::Left;
                    if (hot_ >= 0)
                        OpenTopMenu(size_t(hot_), !backward);
                }
                else if (input.key == K::Enter || input.key == K::Space)
                {
                    if (hot_ >= 0)
                        OpenTopMenu(size_t(hot_), true);
                }
                return true;
            }
            if (input.key == K::Left || input.key == K::Right)
            {
                auto* panel = levels_.back()->panel.get();
                const auto row = panel ? panel->Active() : std::nullopt;
                // Right descends into the active row's submenu when there is one; otherwise
                // both arrows move along the bar. Left at depth 1 moves to the previous menu.
                const bool descend = input.key == K::Right && row && panel->Selectable(*row) &&
                                     !panel->Item(*row).submenu.empty();
                if (descend)
                    OpenSubmenu(levels_.size() - 1, *row);
                else if (levels_.size() == 1)
                    SwitchTopMenu(input.key == K::Right ? 1 : -1);
                else if (input.key == K::Left)
                    CloseLevel(levels_.size() - 1);
                return true;
            }
            auto* panel = levels_.back()->panel.get();
            if (panel && panel->Dispatch(input))
                return true;
            return true;
        }
        bool MenuSession::PointerInput(const Input& input)
        {
            auto* bar = host_.MainMenu();
            if (!bar || !PointerKind(input.kind))
                return false;
            const bool overBar = bar->Bounds().Contains(input.x, input.y);
            const size_t hit = overBar ? bar->HitItem(input.x, input.y) : MenuBar::NoItem;
            const auto usable = [bar](size_t index)
            {
                return index != MenuBar::NoItem && bar->Items()[index].enabled && !bar->Items()[index].items.empty();
            };
            if (levels_.empty() && !active_)
            {
                if (input.kind == InputKind::Down && input.button == LWS::MouseButton::Left && usable(hit))
                {
                    OpenTopMenu(hit, true);
                    menuUsed_ = true;
                    host_.suppressPointerRelease_ = true;
                    return true;
                }
                return input.kind == InputKind::Wheel && overBar;
            }
            if (input.kind == InputKind::Move)
            {
                if (!overBar)
                    return false;
                if (usable(hit))
                {
                    if (int(hit) != hot_)
                    {
                        if (levels_.empty())
                            SetHot(int(hit));
                        else if (CanReuseWindows())
                            SwitchTopMenuTo(hit);
                    }
                }
                else if (levels_.empty())
                    SetHot(-1);
                return true;
            }
            if (input.kind == InputKind::Wheel)
                return overBar;
            if (input.button != LWS::MouseButton::Left)
            {
                if (overBar && input.kind == InputKind::Down)
                {
                    Close();
                    return true;
                }
                if (active_ || !levels_.empty())
                {
                    Close();
                    return false;
                }
                return false;
            }
            if (input.kind == InputKind::Down)
            {
                if (overBar && usable(hit))
                {
                    if (int(hit) == hot_ && !levels_.empty())
                        Close();
                    else
                        OpenTopMenu(hit, true);
                }
                else
                    Close();
                host_.suppressPointerRelease_ = true;
                return true;
            }
            return true;
        }
        bool MenuSession::OnInput(const Input& input)
        {
            auto* bar = host_.MainMenu();
            if (!bar)
                return false;
            if (active_ && bar->orientation != orientationAtOpen_)
                Close();
            if (input.kind == InputKind::KeyDown)
                return KeyDown(input);
            if (input.kind == InputKind::KeyUp)
            {
                if (input.key == LWS::KeyCode::Alt && active_ && !menuUsed_)
                {
                    Close();
                    return true;
                }
                return false;
            }
            if (input.kind == InputKind::Text)
            {
                if (!active_)
                    return false;
                const auto character = !input.text.empty() ? std::optional<char>(input.text[0]) : std::nullopt;
                if (!character)
                    return true;
                menuUsed_ = true;
                return Mnemonic(*character);
            }
            return PointerInput(input);
        }
        bool MenuSession::PanelInput(size_t level, const Input& input)
        {
            if (level >= levels_.size())
                return false;
            Level& entry = *levels_[level];
            entry.dirty = true;
            const bool consumed = entry.panel->Dispatch(input);
            host_.Invalidate();
            return consumed;
        }
        bool MenuSession::OnLevelEvent(LWS::Window* window, const LWS::AnyEvent& event)
        {
            const auto index = FindLevel(window);
            if (!index)
                return false;
            if (std::holds_alternative<LWS::EventPopupDismissed>(event))
            {
                CloseDeepLevels(*index);
                CancelHover();
                return true;
            }
            if (std::holds_alternative<LWS::EventClientAreaSizeChanged>(event) ||
                std::holds_alternative<LWS::EventPaint>(event))
            {
                levels_[*index]->dirty = true;
                host_.Invalidate();
                return false;
            }
            const auto input = InputFromEvent(event, host_.Window().GetPlatformContext());
            if (!input)
                return false;
            if (PointerKind(input->kind))
                return PanelInput(*index, *input);
            return OnInput(*input);
        }
        void MenuSession::PresentLevel(Level& level)
        {
            if (!level.dirty || !level.window || !level.window->IsCreated())
                return;
            const auto metrics = level.window->GetClientAreaMetrics();
            const auto scale = metrics.Scale();
            if (!scale || !metrics.pixels || metrics.pixels->x <= 0 || metrics.pixels->y <= 0)
                return;
            const float width = float(metrics.logical.x), height = float(metrics.logical.y);
            if (width <= 0 || height <= 0)
                return;
            level.logical = {0, 0, width, height};
            level.panel->Measure({width, height});
            level.panel->Arrange({0, 0, width, height});
            canvas_.Begin(metrics.pixels->x, metrics.pixels->y, *scale);
            level.panel->Render(canvas_);
            const auto result = level.window->PresentBitmap(canvas_.End());
            // An unconfigured surface publishes readiness through its own metrics/paint events.
            level.dirty = result != LWS::Result::Success;
        }
        void MenuSession::Update()
        {
            retired_.clear();
            retiredWindows_.clear();
            if (levels_.empty())
                return;
            if (!host_.Window().IsCreated())
            {
                Close();
                return;
            }
            for (auto& level : levels_)
                PresentLevel(*level);
        }
    }  // namespace internal
}  // namespace LWSUI
