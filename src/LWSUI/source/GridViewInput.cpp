#include "GridViewImpl.hpp"
#include <LWSUI/UIHost.hpp>
#include <cmath>
namespace LWSUI
{
    std::vector<ContextMenuItem> GridView::Impl::Menu(const ContextMenuRequest& request)
    {
        auto cell = request.keyboard ? selection.current : owner.CellAt(request.x, request.y);
        if (cell)
        {
            owner.OnContextRequest.Raise(
                {*cell, {request.keyboard ? InputKind::KeyDown : InputKind::Down, request.x, request.y}});
            if (!owner.IsSelected(*cell) && !owner.SelectCell(*cell))
                return {};
        }
        if (!options.contextMenus)
            return {};
        std::vector<ContextMenuItem> items;
        items.push_back({"Copy", "Ctrl+C", [](Control& c) { static_cast<GridView&>(c).CopySelection(); },
                         options.copy && !selection.cells.empty()});
        items.push_back({"Edit cell", "F2",
                         [cell](Control& c)
                         {
                             if (cell)
                                 static_cast<GridView&>(c).BeginEdit(*cell);
                         },
                         cell && Editable(*cell)});
        const auto* geometry = layout.At(request.x);
        if (request.keyboard && selection.current)
            geometry = layout.Find(size_t(Column(selection.current->column) - columns.data()));
        if (geometry)
        {
            const auto column = columns[geometry->index].id;
            items.push_back(ContextMenuItem::Separator());
            items.push_back({"Sort ascending", "", [column](Control& c)
                             { static_cast<GridView&>(c).Sort(column, GridSortDirection::Ascending); },
                             options.sorting && columns[geometry->index].sortable});
            items.push_back({"Sort descending", "", [column](Control& c)
                             { static_cast<GridView&>(c).Sort(column, GridSortDirection::Descending); },
                             options.sorting && columns[geometry->index].sortable});
            items.push_back({"Auto-size column", "",
                             [column](Control& c) { static_cast<GridView&>(c).AutoSizeColumn(column); },
                             options.resizing && columns[geometry->index].resizable});
            items.push_back({"Pin left", "",
                             [column](Control& c) { static_cast<GridView&>(c).PinColumn(column, GridPin::Left); }});
            items.push_back({"Pin right", "", [column](Control& c)
                             { static_cast<GridView&>(c).PinColumn(column, GridPin::Right); }});
            items.push_back({"Unpin", "",
                             [column](Control& c) { static_cast<GridView&>(c).PinColumn(column, GridPin::None); }});
            items.push_back({"Hide column", "",
                             [column](Control& c) { static_cast<GridView&>(c).SetColumnVisible(column, false); }});
        }
        for (const auto& c : columns)
            if (!c.visible)
                items.push_back({"Show " + c.header, "", [id = c.id](Control& control)
                                 { static_cast<GridView&>(control).SetColumnVisible(id, true); }});
        return items;
    }
    bool GridView::OnPreviewInput(const Input& input)
    {
        if (impl_->editor && Host() && Host()->HasFocusWithin(*this) && input.kind == InputKind::KeyDown &&
            !Host()->HasPopupWithin(*this))
            if (input.key == LWS::KeyCode::Escape || input.key == LWS::KeyCode::Enter || input.key == LWS::KeyCode::Tab)
            {
                if (!FinishEdit(input.key == LWS::KeyCode::Escape ? EditPhase::Cancel : EditPhase::Commit))
                    return true;
                Host()->Focus(this);
                if (input.key == LWS::KeyCode::Tab)
                    return OnInput(input);
                return true;
            }
        return Container::OnPreviewInput(input);
    }
    void GridView::OnPointerLeave()
    {
        if (impl_->hovered)
            OnCellLeave.Raise({*impl_->hovered, {InputKind::Move}});
        impl_->hovered.reset();
        impl_->pointerY = -1;
        Invalidate();
    }
    bool GridView::OnInput(const Input& input)
    {
        using K = LWS::KeyCode;
        if (input.kind == InputKind::Cancel)
            return Finish(EditPhase::Cancel);
        if (input.kind == InputKind::Wheel)
        {
            SetPreciseScrollOffset(impl_->offset.width - (input.shift ? input.wheel * Style().scrollStep : 0),
                            impl_->offset.height - (!input.shift ? input.wheel * Style().scrollStep : 0));
            return true;
        }
        if (input.kind == InputKind::Move)
        {
            impl_->pointerX = input.x;
            impl_->pointerY = input.y;
            if (impl_->drag == Impl::Drag::Value)
            {
                impl_->UpdateValueDrag(input);
                return true;
            }
            if (impl_->drag == Impl::Drag::Resize)
            {
                ResizeColumn(impl_->dragColumn, impl_->dragWidth + input.x - impl_->dragStart);
                return true;
            }
            if (impl_->drag == Impl::Drag::Header)
            {
                impl_->moved = impl_->moved || std::abs(input.x - impl_->dragStart) >= 5;
                Invalidate();
                return true;
            }
            auto cell = CellAt(input.x, input.y);
            if (cell != impl_->hovered)
            {
                if (impl_->hovered)
                    OnCellLeave.Raise({*impl_->hovered, input});
                impl_->hovered = cell;
                if (cell)
                    OnCellEnter.Raise({*cell, input});
                Invalidate();
            }
            if (impl_->drag == Impl::Drag::Rows || impl_->drag == Impl::Drag::Cell)
                if ((!cell || *cell != impl_->pressedCell) &&
                    std::hypot(input.x - impl_->pressX, input.y - impl_->pressY) >= 5)
                {
                    impl_->moved = true;
                    impl_->pendingClick.reset();
                }
            if (impl_->drag == Impl::Drag::Rows && impl_->moved && cell)
            {
                SelectCell(*cell, true, input.control);
                return true;
            }
            cursor.reset();
            if (impl_->layout.header.Contains(input.x, input.y) && impl_->options.resizing)
                for (const auto& c : impl_->layout.columns)
                    if (impl_->columns[c.index].resizable && input.x >= c.clip.x && input.x < c.clip.x + c.clip.width &&
                        std::abs(input.x - c.bounds.x - c.bounds.width) <= 4)
                        cursor = LWS::CursorShape::SizeEW;
            return false;
        }
        if (input.kind == InputKind::Down)
        {
            if (input.button != LWS::MouseButton::Left)
                return false;
            if (impl_->layout.header.Contains(input.x, input.y))
            {
                if (!FinishEdit(EditPhase::Commit))
                    return true;
                const auto* geometry = impl_->layout.At(input.x);
                if (!geometry)
                    return true;
                const auto& c = impl_->columns[geometry->index];
                impl_->dragColumn = c.id;
                impl_->dragStart = impl_->pointerX = input.x;
                impl_->moved = false;
                if (impl_->options.resizing)
                    for (const auto& edge : impl_->layout.columns)
                        if (impl_->columns[edge.index].resizable && input.x >= edge.clip.x &&
                            input.x < edge.clip.x + edge.clip.width &&
                            std::abs(input.x - edge.bounds.x - edge.bounds.width) <= 4)
                        {
                            impl_->dragColumn = impl_->columns[edge.index].id;
                            if (input.clickCount >= 2)
                            {
                                AutoSizeColumn(impl_->dragColumn);
                                return true;
                            }
                            impl_->originalWidth = impl_->columns[edge.index].width;
                            impl_->originalMode = impl_->columns[edge.index].widthMode;
                            impl_->dragWidth = edge.bounds.width;
                            impl_->drag = Impl::Drag::Resize;
                            Capture();
                            return true;
                        }
                impl_->drag = Impl::Drag::Header;
                Capture();
                Invalidate();
                return true;
            }
            if (auto cell = CellAt(input.x, input.y))
            {
                const auto clickedBounds = CellBounds(*cell);
                // Pointer selection must not move the target underneath a pending click.
                if (!SelectCellCore(*cell, input.shift, input.control, false, false))
                    return true;
                OnCellClick.Raise({*cell, input});
                impl_->pendingClick.reset();
                const auto* column = impl_->Column(cell->column);
                if (impl_->options.dragEditing && column->dragValue && column->clickValue && impl_->Editable(*cell) &&
                    !input.control && !input.shift && !input.alt)
                {
                    impl_->BeginValueDrag(*cell, input, clickedBounds.value_or(Rect{}));
                    return true;
                }
                if (input.clickCount >= 2)
                {
                    OnCellDoubleClick.Raise({*cell, input});
                    if (impl_->options.doubleClickEdit && !impl_->options.singleClickEdit && !input.control &&
                        !input.shift && !input.alt)
                        BeginEdit(*cell);
                }
                if (input.clickCount < 2 || impl_->options.singleClickEdit)
                {
                    if (impl_->options.singleClickEdit && !input.control && !input.shift && !input.alt &&
                        impl_->Editable(*cell))
                    {
                        impl_->pendingClick = cell;
                        impl_->pressedCellBounds = clickedBounds.value_or(Rect{});
                    }
                    if (impl_->options.multipleSelection || impl_->pendingClick)
                    {
                        impl_->drag = impl_->options.multipleSelection ? Impl::Drag::Rows : Impl::Drag::Cell;
                        impl_->pressedCell = *cell;
                        impl_->pressX = input.x;
                        impl_->pressY = input.y;
                        impl_->moved = false;
                        Capture();
                    }
                }
                return true;
            }
        }
        if (input.kind == InputKind::Up && input.button == LWS::MouseButton::Left && impl_->drag != Impl::Drag::None)
        {
            if (impl_->drag == Impl::Drag::Value)
            {
                impl_->UpdateValueDrag(input);
                impl_->FinishValueDrag(EditPhase::Commit);
                return true;
            }
            const auto drag = impl_->drag;
            const auto click = impl_->pendingClick;
            impl_->pendingClick.reset();
            impl_->drag = Impl::Drag::None;
            ReleaseCapture();
            if (click && !impl_->moved && impl_->options.singleClickEdit && !input.control && !input.shift &&
                !input.alt && CellAt(input.x, input.y) == click)
                impl_->ActivateClick(*click, input);
            if (drag == Impl::Drag::Resize)
            {
                GridColumnEvent event;
                event.column = impl_->dragColumn;
                event.oldWidth = impl_->originalWidth;
                if (auto* c = impl_->Column(impl_->dragColumn))
                    event.newWidth = c->width;
                OnColumnResized.Raise(event);
            }
            if (drag == Impl::Drag::Header)
            {
                const auto* geometry = impl_->layout.At(input.x);
                if (impl_->moved)
                {
                    if (geometry && impl_->layout.header.Contains(input.x, input.y))
                        MoveColumn(impl_->dragColumn, geometry->index);
                }
                else if (impl_->layout.header.Contains(input.x, input.y))
                {
                    GridColumnEvent event;
                    event.column = impl_->dragColumn;
                    OnHeaderClick.Raise(event);
                    const auto next = impl_->sortColumn != impl_->dragColumn             ? GridSortDirection::Ascending
                                      : impl_->direction == GridSortDirection::Ascending ? GridSortDirection::Descending
                                                                                         : GridSortDirection::None;
                    Sort(impl_->dragColumn, next);
                }
            }
            impl_->dragColumn.clear();
            impl_->moved = false;
            Invalidate();
            return true;
        }
        if (input.kind == InputKind::Text && impl_->options.typingEdit && impl_->selection.current)
        {
            if (!BeginEdit(*impl_->selection.current))
                return false;
            auto& field = impl_->editor->FocusTarget();
            if (auto* text = dynamic_cast<TextBox*>(&field))
                text->SetText({});
            field.Dispatch(input);
            return true;
        }
        if (input.kind != InputKind::KeyDown)
            return false;
        if (input.key == K::Escape)
        {
            Finish(EditPhase::Cancel);
            return true;
        }
        if (input.control && (input.key == K::C || (!input.shift && !input.alt && input.key == K::Insert)))
            return CopySelection();
        if (input.control && input.key == K::A)
        {
            SelectAll();
            return true;
        }
        if (input.key == K::F2 && impl_->options.f2Edit && impl_->selection.current)
            return BeginEdit(*impl_->selection.current);
        if (input.control && input.key == K::Space && impl_->selection.current)
            return SelectCell(*impl_->selection.current, false, true);
        if (!impl_->model || !impl_->model->RowCount())
            return false;
        std::vector<const GridColumn*> visible;
        for (const auto& c : impl_->columns)
            if (c.visible)
                visible.push_back(&c);
        if (visible.empty())
            return false;
        size_t row = 0, col = 0;
        if (impl_->selection.current)
        {
            row = impl_->projection.Index(*impl_->model, impl_->selection.current->row).value_or(0);
            auto it = std::ranges::find_if(visible,
                                           [&](const auto* c) { return c->id == impl_->selection.current->column; });
            if (it != visible.end())
                col = size_t(it - visible.begin());
        }
        const auto page = std::max(size_t(1), size_t(impl_->layout.body.height / impl_->options.rowHeight));
        const auto last = impl_->model->RowCount() - 1;
        if (input.key == K::Up)
            row = row ? row - 1 : 0;
        else if (input.key == K::Down)
            row = std::min(last, row + 1);
        else if (input.key == K::Left)
            col = col ? col - 1 : 0;
        else if (input.key == K::Right)
            col = std::min(visible.size() - 1, col + 1);
        else if (input.key == K::PageUp)
            row = row > page ? row - page : 0;
        else if (input.key == K::PageDown)
            row = std::min(last, row + page);
        else if (input.key == K::Home)
        {
            col = 0;
            if (input.control)
                row = 0;
        }
        else if (input.key == K::End)
        {
            col = visible.size() - 1;
            if (input.control)
                row = last;
        }
        else if (input.key == K::Tab && impl_->options.tabNavigation)
        {
            if (input.shift)
            {
                if (col)
                    --col;
                else if (row)
                {
                    --row;
                    col = visible.size() - 1;
                }
                else
                    return false;
            }
            else
            {
                if (col + 1 < visible.size())
                    ++col;
                else if (row < last)
                {
                    ++row;
                    col = 0;
                }
                else
                    return false;
            }
        }
        else
            return false;
        return SelectCell({DisplayRowId(row), visible[col]->id}, input.shift && input.key != K::Tab, false,
                          input.control && !input.shift && (input.key == K::Up || input.key == K::Down));
    }
}  // namespace LWSUI
