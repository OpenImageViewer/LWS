#pragma once
#include <LWSUI/Containers.hpp>
#include <LWSUI/UIHost.hpp>
#include "MenuRows.hpp"
namespace LWSUI::internal
{
    // One flat command overlay. Submenus and a global command registry are deliberately deferred.
    class ContextMenu final : public ScrollView
    {
        class Row final : public Control
        {
          public:

            Row(const ContextMenuItem& item, bool checks) : item_(item), checks_(checks) {}
            bool active = false;

          protected:

            Size OnMeasure(Size available) override
            {
                const auto& s = Style();
                if (item_.separator)
                    return {available.width, 2 * s.menuPadding + s.borderWidth};
                const float h = Host()->MeasureText(item_.label, LabelWidth(available.width), true, Font()).height;
                return {available.width, std::max(s.menuRowHeight, h + 2 * s.menuPadding)};
            }
            void OnRender(Canvas& canvas) override
            {
                const auto b = Bounds();
                const auto& s = Style();
                const float p = s.menuPadding;
                if (item_.separator)
                {
                    RenderMenuSeparator(*this, canvas);
                    return;
                }
                if (active)
                    canvas.Fill(b.x, b.y, b.width, b.height, s.selection);
                const auto color = item_.enabled && item_.action ? s.foreground : s.muted;
                const float mark = checks_ ? Font().size + p : 0;
                if (item_.checked.value_or(false))
                    RenderMenuCheck(*this, canvas, color);
                const float labelWidth = LabelWidth(b.width);
                canvas.Text(item_.label, b.x + p + mark, b.y + p, labelWidth, b.height - 2 * p, color, Font(), true);
                const float hint = HintWidth(b.width);
                if (hint > 0)
                    canvas.Text(item_.shortcut, b.x + b.width - p - hint, b.y + p, hint, b.height - 2 * p, s.muted,
                                Font());
            }

          private:

            float HintWidth(float width) const { return MenuHintWidth(*this, item_.shortcut, width); }
            float LabelWidth(float width) const
            {
                const float p = Style().menuPadding, hint = HintWidth(width);
                return std::max(1.f, width - 2 * p - (checks_ ? Font().size + p : 0) - hint - (hint > 0 ? 2 * p : 0));
            }
            const ContextMenuItem& item_;
            bool checks_;
        };

      public:

        ContextMenu(std::vector<ContextMenuItem> items, std::function<void(std::function<void(Control&)>)> chosen,
                    bool keyboard)
            : items_(std::move(items)), chosen_(std::move(chosen))
        {
            auto list = std::make_unique<StackPanel>();
            list->spacing = 0;
            const bool checks = std::ranges::any_of(items_, [](const auto& i) { return i.checked.has_value(); });
            for (const auto& item : items_)
                rows_.push_back(&list->Emplace<Row>(item, checks));
            SetContent(std::move(list));
            if (keyboard)
                Select(First());
        }
        float NaturalWidth(UIHost& host) const
        {
            const auto& s = Style();
            float label = 0, hint = 0;
            bool checks = false;
            for (const auto& item : items_)
            {
                label = std::max(label, host.MeasureText(item.label, 10000, false, Font()).width);
                hint = std::max(hint, host.MeasureText(item.shortcut, 10000, false, Font()).width);
                checks |= item.checked.has_value();
            }
            return label + hint + 4 * s.menuPadding + (checks ? Font().size + s.menuPadding : 0) + s.scrollbarWidth +
                   s.scrollbarMargin;
        }

      protected:

        void OnRender(Canvas& canvas) override
        {
            const auto b = Bounds();
            canvas.Fill(b.x, b.y, b.width, b.height, Style().background);
            ScrollView::OnRender(canvas);
        }
        bool OnInput(const Input& input) override
        {
            using K = LWS::KeyCode;
            if (input.kind == InputKind::KeyDown)
            {
                if (input.key == K::Home)
                    Select(First());
                else if (input.key == K::End)
                    Select(Next(items_.size(), -1));
                else if (input.key == K::Up || input.key == K::Down)
                    Select(Next(active_.value_or(input.key == K::Up ? 0 : items_.size() - 1),
                                input.key == K::Up ? -1 : 1));
                else if ((input.key == K::Enter || input.key == K::Space) && !input.repeat && active_)
                    chosen_(items_[*active_].action);
                return true;
            }
            if (input.kind == InputKind::Cancel)
            {
                pressed_.reset();
                ReleaseCapture();
                return true;
            }
            if (input.kind == InputKind::Move || input.kind == InputKind::Down || input.kind == InputKind::Up)
            {
                std::optional<size_t> hit;
                if (Bounds().Contains(input.x, input.y))
                    for (size_t i = 0; i < rows_.size(); ++i)
                        if (Selectable(i) && rows_[i]->Bounds().Contains(input.x, input.y))
                        {
                            hit = i;
                            break;
                        }
                Select(hit, false);
                if (input.kind == InputKind::Down)
                {
                    pressed_ = hit;
                    Capture();
                }
                if (input.kind == InputKind::Up)
                {
                    const bool activate = hit && hit == pressed_;
                    pressed_.reset();
                    ReleaseCapture();
                    if (activate)
                        chosen_(items_[*hit].action);
                }
                return true;
            }
            return ScrollView::OnInput(input);
        }

      private:

        bool Selectable(size_t index) const
        {
            return !items_[index].separator && items_[index].enabled && bool(items_[index].action);
        }
        std::optional<size_t> First() const { return Next(items_.size() - 1, 1); }
        std::optional<size_t> Next(size_t index, int step) const
        {
            return NextMenuRow(items_.size(), index, step, [this](size_t row) { return Selectable(row); });
        }
        void Select(std::optional<size_t> index, bool reveal = true)
        {
            SelectMenuRow(*this, rows_, active_, index);
            if (reveal)
                RevealMenuRow(*this, rows_, active_);
        }
        std::vector<ContextMenuItem> items_;
        std::vector<Row*> rows_;
        std::function<void(std::function<void(Control&)>)> chosen_;
        std::optional<size_t> active_, pressed_;
    };
}  // namespace LWSUI::internal
