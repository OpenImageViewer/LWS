#include "GridViewImpl.hpp"
#include <LWSUI/internal/TextPresentation.hpp>
#include <LWSUI/UIHost.hpp>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    FontSpec GridView::Impl::HeaderFont() const { return appearance.headerFont.value_or(owner.Style().headingFont); }
    float GridView::Impl::ContentWidth(const GridColumn& column) const
    {
        const auto* definition = Definition(column.id);
        if (!definition)
            return column.width;
        const auto measure = [&](const std::string& text, const FontSpec& font)
        {
            return owner.Host() ? owner.Host()->MeasureText(text, 100000.f, false, font).width
                                : float(text.size()) * font.size * .65f;
        };
        float width = measure(column.header, HeaderFont()) +
                      2 * appearance.headerPadding + 18;
        for (size_t i = 0; i < model->RowCount(); ++i)
            width = std::max(width, measure(internal::GridText(column, *definition,
                                                               model->Value({model->RowId(i), column.id})),
                                            owner.Font()) +
                                        2 * appearance.cellPadding);
        return std::clamp(width, column.minimumWidth, column.maximumWidth);
    }
    void GridView::Impl::SizeContentColumns()
    {
        for (auto& column : columns)
            if (column.widthMode == GridWidthMode::Content)
                column.width = ContentWidth(column);
    }
    void GridView::Impl::Reveal(const GridCell& cell)
    {
        auto index = projection.Index(*model, cell.row);
        if (!index)
            return;
        float y = offset.height;
        const float top = float(double(*index) * options.rowHeight) +
                          (options.headerVisible && !options.stickyHeaders ? options.headerHeight : 0);
        if (top < y)
            y = top;
        if (top + options.rowHeight > y + layout.scrollPageHeight)
            y = top + options.rowHeight - layout.scrollPageHeight;
        float x = offset.width, position = 0;
        for (const auto& c : columns)
        {
            if (!c.visible || c.pin != GridPin::None)
                continue;
            const auto* geometry = layout.Find(size_t(&c - columns.data()));
            const float width = geometry ? geometry->bounds.width
                                         : std::clamp(c.width, c.minimumWidth, c.maximumWidth);
            if (c.id == cell.column)
            {
                if (position < x)
                    x = position;
                else if (position + width > x + layout.center.width)
                    x = position + width - layout.center.width;
                break;
            }
            position += width;
        }
        owner.SetScrollOffset(x, y);
    }
    void GridView::SetScrollOffset(float x, float y)
    {
        if (!std::isfinite(x) || !std::isfinite(y))
            throw std::invalid_argument("Scroll offsets must be finite");
        x = std::clamp(x, 0.f, impl_->layout.horizontalExtent);
        y = std::clamp(y, 0.f, impl_->layout.VerticalLimit());
        if (x == impl_->offset.width && y == impl_->offset.height)
            return;
        if (!FinishEdit(EditPhase::Commit))
            return;
        impl_->offset = {x, y};
        OnArrange();
        Invalidate();
        OnScroll.Raise(impl_->offset);
    }
    Size GridView::ScrollOffset() const
    {
        return impl_->offset;
    }
    size_t GridView::FirstVisibleRow() const
    {
        return impl_->model ? std::min(impl_->model->RowCount(),
                                       size_t(std::floor(impl_->layout.rowOffset / impl_->options.rowHeight)))
                            : 0;
    }
    size_t GridView::VisibleRowCount() const
    {
        if (!impl_->model || impl_->layout.body.height <= 0)
            return 0;
        return std::min(impl_->model->RowCount() - FirstVisibleRow(),
                        size_t(std::ceil(impl_->layout.body.height / impl_->options.rowHeight)) + 1);
    }
    std::optional<Rect> GridView::CellBounds(const GridCell& cell) const
    {
        if (!impl_->Valid(cell))
            return {};
        const auto* c = impl_->Column(cell.column);
        const auto* geometry = impl_->layout.Find(size_t(c - impl_->columns.data()));
        auto row = impl_->projection.Index(*impl_->model, cell.row);
        if (!geometry || !row || geometry->bounds.x >= geometry->clip.x + geometry->clip.width ||
            geometry->bounds.x + geometry->bounds.width <= geometry->clip.x)
            return {};
        Rect bounds{geometry->bounds.x, impl_->layout.RowY(*row, impl_->options.rowHeight), geometry->bounds.width,
                    impl_->options.rowHeight};
        if (bounds.y >= impl_->layout.body.y + impl_->layout.body.height ||
            bounds.y + bounds.height <= impl_->layout.body.y)
            return {};
        return bounds;
    }
    std::optional<GridCell> GridView::CellAt(float x, float y) const
    {
        if (!impl_->model || !impl_->layout.body.Contains(x, y))
            return {};
        const auto* c = impl_->layout.At(x);
        if (!c)
            return {};
        const auto row = size_t(
            std::floor((y - impl_->layout.body.y + impl_->layout.rowOffset) / impl_->options.rowHeight));
        if (row >= impl_->model->RowCount())
            return {};
        return GridCell{DisplayRowId(row), impl_->columns[c->index].id};
    }
    void GridView::OnArrange()
    {
        const auto b = Bounds();
        const float bar = Style().scrollbarWidth;
        if (impl_->drag != Impl::Drag::None &&
            (b.width != impl_->arrangedSize.width || b.height != impl_->arrangedSize.height))
            impl_->CancelDrag();
        impl_->arrangedSize = {b.width, b.height};
        impl_->layout.Build(b, impl_->columns, impl_->options, impl_->model ? impl_->model->RowCount() : 0, bar,
                            impl_->offset.width, impl_->offset.height);
        const Size adjusted{std::clamp(impl_->offset.width, 0.f, impl_->layout.horizontalExtent),
                            std::clamp(impl_->offset.height, 0.f, impl_->layout.VerticalLimit())};
        if (adjusted.width != impl_->offset.width || adjusted.height != impl_->offset.height)
        {
            impl_->offset = adjusted;
            impl_->layout.Build(b, impl_->columns, impl_->options, impl_->model ? impl_->model->RowCount() : 0, bar,
                                adjusted.width, adjusted.height);
        }
        impl_->vertical->SetVisible(impl_->layout.verticalBar);
        impl_->horizontal->SetVisible(impl_->layout.horizontalBar);
        impl_->vertical->SetPage(impl_->layout.scrollPageHeight / std::max(1.f, impl_->layout.verticalExtent));
        impl_->horizontal->SetPage(impl_->layout.center.width /
                                   std::max(1.f, impl_->layout.center.width + impl_->layout.horizontalExtent));
        impl_->vertical->SetValue(impl_->offset.height / std::max(1.f, impl_->layout.VerticalLimit()));
        impl_->horizontal->SetValue(impl_->offset.width / std::max(1.f, impl_->layout.horizontalExtent));
        impl_->vertical->Arrange({b.x + impl_->layout.body.width,
                                  impl_->options.stickyHeaders ? impl_->layout.body.y : impl_->layout.viewport.y, bar,
                                  impl_->layout.scrollPageHeight});
        impl_->horizontal->Arrange({impl_->layout.center.x, impl_->layout.body.y + impl_->layout.body.height,
                                    impl_->layout.center.width, bar});
        if (impl_->editor)
        {
            if (auto cell = CellBounds(impl_->editCell))
            {
                const auto* col = impl_->layout.Find(
                    size_t(impl_->Column(impl_->editCell.column) - impl_->columns.data()));
                const float left = std::max(cell->x, col->clip.x),
                            right = std::min(cell->x + cell->width, col->clip.x + col->clip.width);
                const float top = std::max(cell->y, impl_->layout.body.y),
                            bottom = std::min(cell->y + cell->height, impl_->layout.body.y + impl_->layout.body.height);
                Rect bounds{left, top, std::max(0.f, right - left), std::max(0.f, bottom - top)};
                impl_->editor->Measure({bounds.width, bounds.height});
                impl_->editor->Arrange(bounds);
            }
            else
                FinishEdit(EditPhase::Cancel);
        }
    }
    static void AlignedText(Canvas& canvas, std::string_view text, Rect b, Alignment alignment, float padding,
                            LLUtils::Color color, const FontSpec& font)
    {
        b.x += padding;
        b.width = std::max(0.f, b.width - 2 * padding);
        if (b.width <= 0 || b.height <= 0)
            return;
        Canvas::ClipScope clip(canvas, b.x, b.y, b.width, b.height);
        const auto textBounds = internal::AlignedTextBounds(canvas, text, b, font, alignment, Alignment::Center);
        canvas.Text(text, textBounds.x, textBounds.y, textBounds.width, textBounds.height, color, font);
    }
    void GridView::OnRender(Canvas& canvas)
    {
        const auto& theme = Style();
        const auto& a = impl_->appearance;
        const auto& o = impl_->options;
        const auto& layout = impl_->layout;
        const auto line = a.line.value_or(theme.line);
        canvas.Fill(Bounds().x, Bounds().y, Bounds().width, Bounds().height, a.cellBackground.value_or(theme.surface));
        for (const auto& geometry : layout.columns)
        {
            if (geometry.clip.width <= 0 || geometry.bounds.x >= geometry.clip.x + geometry.clip.width ||
                geometry.bounds.x + geometry.bounds.width <= geometry.clip.x)
                continue;
            const auto& column = impl_->columns[geometry.index];
            Canvas::ClipScope region(canvas, geometry.clip.x, layout.viewport.y, geometry.clip.width,
                                     layout.viewport.height);
            if (o.headerVisible && layout.header.y + layout.header.height > layout.viewport.y)
            {
                Rect header{geometry.bounds.x, layout.header.y, geometry.bounds.width, layout.header.height};
                auto background = column.headerBackground.value_or(a.headerBackground.value_or(theme.background));
                if (impl_->dragColumn == column.id && impl_->drag != Impl::Drag::None)
                    background = a.headerPressed.value_or(theme.selectedOption);
                else if (Hovered() && layout.header.Contains(impl_->pointerX, impl_->pointerY) &&
                         geometry.bounds.x <= impl_->pointerX &&
                         impl_->pointerX < geometry.bounds.x + geometry.bounds.width)
                    background = a.headerHover.value_or(theme.hoverSurface);
                canvas.Fill(header.x, header.y, header.width, header.height, background);
                auto textBounds = header;
                const bool sorted = o.sortGlyphs && impl_->sortColumn == column.id &&
                                    impl_->direction != GridSortDirection::None;
                if (sorted)
                    textBounds.width = std::max(0.f, textBounds.width - 18);
                AlignedText(canvas, column.header, textBounds, a.headerAlignment, a.headerPadding,
                            column.headerForeground.value_or(a.headerForeground.value_or(theme.foreground)),
                            impl_->HeaderFont());
                if (sorted)
                {
                    const float x = header.x + header.width - 12, y = header.y + header.height / 2;
                    const float dy = impl_->direction == GridSortDirection::Ascending ? -3.f : 3.f;
                    canvas.Line(x - 4, y - dy, x, y + dy, 1.5f, theme.accent);
                    canvas.Line(x, y + dy, x + 4, y - dy, 1.5f, theme.accent);
                }
                canvas.Line(header.x, header.y + header.height - 1, header.x + header.width,
                            header.y + header.height - 1, a.borderWidth, a.headerBorder.value_or(line));
                canvas.Line(header.x + header.width - 1, header.y, header.x + header.width - 1,
                            header.y + header.height, a.borderWidth, a.headerBorder.value_or(line));
            }
            Canvas::ClipScope body(canvas, geometry.clip.x, layout.body.y, geometry.clip.width, layout.body.height);
            const auto* definition = impl_->Definition(column.id);
            if (!definition)
                continue;
            for (size_t i = FirstVisibleRow(), end = i + VisibleRowCount(); i < end; ++i)
            {
                const GridCell cell{DisplayRowId(i), column.id};
                const Rect bounds{geometry.bounds.x, layout.RowY(i, o.rowHeight), geometry.bounds.width, o.rowHeight};
                const bool selected = IsSelected(cell),
                           hovered = impl_->hovered &&
                                     (o.fullRowSelect ? impl_->hovered->row == cell.row : *impl_->hovered == cell);
                const bool current = impl_->selection.current == cell;
                auto background = a.cellBackground.value_or(theme.surface);
                if (o.striped && i % 2)
                    background = a.alternateRow.value_or(theme.background);
                if (hovered)
                    background = a.hovered.value_or(theme.hoveredRow);
                if (selected)
                    background = a.selected.value_or(theme.selectedRow);
                canvas.Fill(bounds.x, bounds.y, bounds.width, bounds.height, background);
                const auto value = impl_->drag == Impl::Drag::Value && impl_->valueCell == cell
                                       ? impl_->valueDraft
                                       : impl_->model->Value(cell);
                const auto text = internal::GridText(column, *definition, value);
                Canvas::ClipScope cellClip(canvas, bounds.x, bounds.y, bounds.width, bounds.height);
                if (column.render)
                    column.render(canvas, {cell, value, bounds, text, selected, hovered, current});
                else if (definition->type == GridValueType::Boolean)
                {
                    const float size = std::min(16.f, std::max(0.f, bounds.height - 8));
                    const float x = bounds.x + a.cellPadding, y = bounds.y + (bounds.height - size) / 2;
                    canvas.Fill(x, y, size, size, line);
                    canvas.Fill(x + 1, y + 1, size - 2, size - 2, background);
                    if (std::get<bool>(value))
                    {
                        canvas.Line(x + 3, y + size / 2, x + size / 2, y + size - 4, 2, theme.accent);
                        canvas.Line(x + size / 2, y + size - 4, x + size - 3, y + 3, 2, theme.accent);
                    }
                }
                else
                    AlignedText(canvas, text, bounds, column.alignment, a.cellPadding,
                                a.cellForeground.value_or(Foreground()), Font());
                if (o.horizontalLines)
                    canvas.Line(bounds.x, bounds.y + bounds.height - 1, bounds.x + bounds.width,
                                bounds.y + bounds.height - 1, a.borderWidth, line);
                if (o.verticalLines)
                    canvas.Line(bounds.x + bounds.width - 1, bounds.y, bounds.x + bounds.width - 1,
                                bounds.y + bounds.height, a.borderWidth, line);
                if (current && (!Host() || Host()->HasFocusWithin(*this)))
                {
                    const auto color = a.focus.value_or(theme.accent);
                    canvas.Line(bounds.x, bounds.y, bounds.x + bounds.width, bounds.y, theme.focusWidth, color);
                    canvas.Line(bounds.x, bounds.y + bounds.height - 1, bounds.x + bounds.width,
                                bounds.y + bounds.height - 1, theme.focusWidth, color);
                    canvas.Line(bounds.x, bounds.y, bounds.x, bounds.y + bounds.height, theme.focusWidth, color);
                    canvas.Line(bounds.x + bounds.width - 1, bounds.y, bounds.x + bounds.width - 1,
                                bounds.y + bounds.height, theme.focusWidth, color);
                }
            }
        }
        if (!impl_->model || impl_->model->RowCount() == 0)
            AlignedText(canvas, o.emptyText, layout.body, Alignment::Center, 8, theme.muted, Font());
        for (float x : {layout.center.x, layout.right.x})
            if (x > layout.body.x && x < layout.body.x + layout.body.width)
                canvas.Line(x, layout.viewport.y, x, layout.body.y + layout.body.height, 2, line);
        if (impl_->drag == Impl::Drag::Header && impl_->moved && impl_->options.reordering &&
            impl_->Column(impl_->dragColumn)->reorderable)
        {
            if (const auto* geometry = layout.At(impl_->pointerX))
                canvas.Line(geometry->bounds.x, layout.header.y, geometry->bounds.x, layout.body.y + layout.body.height,
                            3, theme.accent);
            Rect preview{impl_->pointerX + 8, layout.header.y + 3, 140, layout.header.height - 6};
            canvas.Fill(preview.x, preview.y, preview.width, preview.height, theme.popupSurface);
            if (auto* c = impl_->Column(impl_->dragColumn))
                AlignedText(canvas, c->header, preview, Alignment::Start, 6, theme.foreground, theme.font);
        }
        if (impl_->editor)
            impl_->editor->Render(canvas);
        impl_->vertical->Render(canvas);
        impl_->horizontal->Render(canvas);
    }
}  // namespace LWSUI
