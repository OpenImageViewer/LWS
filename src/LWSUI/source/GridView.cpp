#include "GridViewImpl.hpp"
#include <LWSUI/internal/ScopedFlag.hpp>
#include <LWSUI/UIHost.hpp>
#include <LWS/TextClipboard.hpp>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    GridView::Impl::Impl(GridView& grid) : owner(grid) {}
    GridColumn* GridView::Impl::Column(const GridColumnId& id)
    {
        auto it = std::ranges::find(columns, id, &GridColumn::id);
        return it == columns.end() ? nullptr : &*it;
    }
    const GridModelColumn* GridView::Impl::Definition(const GridColumnId& id) const
    {
        if (!model)
            return nullptr;
        auto definitions = model->Columns();
        auto it = std::ranges::find(definitions, id, &GridModelColumn::id);
        return it == definitions.end() ? nullptr : &*it;
    }
    bool GridView::Impl::Valid(const GridCell& cell) const
    {
        return model && model->IndexOf(cell.row) && Definition(cell.column) &&
               std::ranges::any_of(columns, [&](const GridColumn& c) { return c.id == cell.column && c.visible; });
    }
    bool GridView::Impl::Editable(const GridCell& cell)
    {
        auto* c = Column(cell.column);
        return Valid(cell) && options.editing && !options.readOnly && c && c->editable && model->Editable(cell);
    }
    void GridView::Impl::Rebuild()
    {
        if (model)
            projection.Rebuild(*model, Column(sortColumn), direction);
    }
    void GridView::Impl::Publish(GridSelectionChange change)
    {
        if (!change.added.empty() || !change.removed.empty())
            owner.OnSelectionChanged.Raise(change);
        owner.Invalidate();
    }
    GridStatus GridView::Impl::RequestChange(GridValueChange& change, bool notify)
    {
        if (notify && change.oldValue != change.newValue)
            owner.OnCellValueChanging.Raise(change);
        if (change.cancel)
            return std::unexpected(change.error.empty() ? "Cell change was rejected" : change.error);
        if (change.origin == GridOrigin::User && !Editable(change.cell))
            return std::unexpected("Cell is read-only");
        return {};
    }
    GridStatus GridView::Impl::StoreChange(GridValueChange& change)
    {
        const internal::ScopedFlag restore(applying, applying || change.origin == GridOrigin::User);
        auto status = model->SetValue(change.cell, change.newValue);
        if (status)
            change.newValue = model->Value(change.cell);
        return status;
    }
    void GridView::Impl::PublishChange(const GridValueChange& change, bool notify)
    {
        if (notify && change.oldValue != change.newValue)
            owner.OnCellValueChanged.Raise(change);
        owner.Invalidate();
    }
    bool GridView::Impl::ActivateClick(const GridCell& cell, const Input& input)
    {
        if (!Editable(cell) || !owner.FinishEdit(EditPhase::Commit))
            return false;
        const auto* column = Column(cell.column);
        const auto old = model->Value(cell);
        std::optional<GridValue> value;
        if (column->clickValue)
            value = column->clickValue({cell, old, pressedCellBounds,
                                        internal::GridText(*column, *Definition(cell.column), old),
                                        owner.IsSelected(cell), hovered == cell, selection.current == cell},
                                       input);
        else if (Definition(cell.column)->type == GridValueType::Boolean)
            value = !std::get<bool>(old);
        if (!value)
            return owner.BeginEdit(cell);
        GridEditRequest request{cell};
        owner.OnEditBeginning.Raise(request);
        if (request.cancel || !Editable(cell))
            return false;
        owner.OnEdit.Raise({cell, *value, EditPhase::Preview});
        GridValueChange change{cell, model->Value(cell), *value, GridOrigin::User};
        auto status = RequestChange(change, true);
        if (status)
            status = StoreChange(change);
        if (!status)
        {
            owner.OnValidationFailed.Raise({cell, status.error()});
            owner.OnEdit.Raise({cell, model->Value(cell), EditPhase::Cancel});
            return false;
        }
        PublishChange(change, true);
        owner.OnEdit.Raise({cell, change.newValue, EditPhase::Commit});
        return true;
    }
    bool GridView::Impl::BeginValueDrag(const GridCell& cell, const Input& input, Rect bounds)
    {
        auto* column = Column(cell.column);
        if (!options.dragEditing || !column || !column->dragValue || !column->clickValue || !Editable(cell) ||
            input.control || input.shift || input.alt || !owner.FinishEdit(EditPhase::Commit))
            return false;
        auto initial = model->Value(cell);
        auto draft = column->clickValue({cell, initial, bounds,
                                         internal::GridText(*column, *Definition(cell.column), initial),
                                         owner.IsSelected(cell), hovered == cell, selection.current == cell},
                                        input);
        if (!draft)
            return false;
        if (auto status = ValidateGridValue(*Definition(cell.column), *draft); !status)
        {
            owner.OnValidationFailed.Raise({cell, status.error()});
            return false;
        }
        GridEditRequest request{cell};
        owner.OnEditBeginning.Raise(request);
        if (request.cancel || !Editable(cell) || !options.dragEditing)
            return false;
        valueCell = cell;
        valueStart = model->Value(cell);
        valueDraft = *draft;
        pressedCellBounds = bounds;
        pendingClick.reset();
        drag = Drag::Value;
        owner.Capture();
        owner.OnEdit.Raise({cell, valueDraft, EditPhase::Preview});
        owner.Invalidate();
        return true;
    }
    void GridView::Impl::UpdateValueDrag(const Input& input)
    {
        if (drag != Drag::Value)
            return;
        auto* column = Column(valueCell.column);
        if (!column || !column->clickValue || !Editable(valueCell) || !options.dragEditing)
        {
            CancelDrag();
            return;
        }
        auto draft = column->clickValue({valueCell, valueDraft, pressedCellBounds,
                                         internal::GridText(*column, *Definition(valueCell.column), valueDraft),
                                         owner.IsSelected(valueCell), hovered == valueCell,
                                         selection.current == valueCell},
                                        input);
        if (!draft || *draft == valueDraft)
            return;
        if (auto status = ValidateGridValue(*Definition(valueCell.column), *draft); !status)
        {
            owner.OnValidationFailed.Raise({valueCell, status.error()});
            return;
        }
        valueDraft = *draft;
        owner.OnEdit.Raise({valueCell, valueDraft, EditPhase::Preview});
        owner.Invalidate();
    }
    bool GridView::Impl::FinishValueDrag(EditPhase phase)
    {
        if (drag != Drag::Value || finishing)
            return true;
        if (phase == EditPhase::Preview)
            return true;
        if (phase == EditPhase::Cancel)
        {
            CancelDrag();
            return true;
        }
        const internal::ScopedFlag guard(finishing, true);
        const auto cell = valueCell;
        GridValueChange change{cell, valueStart, valueDraft, GridOrigin::User};
        auto status = RequestChange(change, true);
        if (drag != Drag::Value)
            return false;
        if (!status)
        {
            owner.OnValidationFailed.Raise({cell, status.error()});
            CancelDrag();
            return false;
        }
        // Once accepted, model observers see a completed gesture rather than a live capture.
        drag = Drag::None;
        owner.ReleaseCapture();
        status = StoreChange(change);
        if (!status)
        {
            owner.OnValidationFailed.Raise({cell, status.error()});
            owner.OnEdit.Raise({cell, change.oldValue, EditPhase::Cancel});
            owner.Invalidate();
            return false;
        }
        PublishChange(change, true);
        owner.OnEdit.Raise({cell, change.newValue, EditPhase::Commit});
        return true;
    }
    void GridView::Impl::RetireEditor()
    {
        if (!editor)
            return;
        auto* previous = editor;
        editConnection.Disconnect();
        editor->SetVisible(false);
        editor = nullptr;
        retired.push_back(previous);
        if (owner.Host())
            owner.Host()->Post(owner.Handle(),
                               [](Control& control) { static_cast<GridView&>(control).impl_->Cleanup(); });
        owner.Invalidate();
    }
    void GridView::Impl::Cleanup()
    {
        auto handles = std::move(retired);
        retired.clear();
        for (auto* c : handles)
            if (std::ranges::any_of(owner.Children(), [c](const auto& child) { return child.get() == c; }))
                owner.Remove(*c);
    }
    bool GridView::Impl::Error(std::string message)
    {
        if (editor)
            editor->SetValidation(message);
        owner.OnValidationFailed.Raise({editCell, std::move(message)});
        owner.Invalidate();
        return false;
    }
    void GridView::Impl::ModelChanged(const GridModelChange& change)
    {
        if (drag == Drag::Value && !applying &&
            (change.kind == GridModelChange::Kind::Structure || change.cell == valueCell))
            CancelDrag();
        if (editor && !applying && (change.kind == GridModelChange::Kind::Structure || change.cell == editCell))
            owner.FinishEdit(EditPhase::Cancel);
        if (change.kind == GridModelChange::Kind::Structure)
        {
            std::erase_if(columns, [&](const GridColumn& column) { return !Definition(column.id); });
            if (!Column(sortColumn))
            {
                sortColumn.clear();
                direction = GridSortDirection::None;
            }
            auto next = selection.cells;
            std::erase_if(
                next, [&](const GridCell& cell)
                { return !model->IndexOf(cell.row) || (!cell.column.empty() && !Definition(cell.column)); });
            Publish(selection.Replace(std::move(next)));
            if (selection.current && !Valid(*selection.current))
            {
                selection.current.reset();
                selection.anchor.reset();
                owner.OnCurrentCellChanged.Raise(selection.current);
            }
        }
        for (auto& column : columns)
            if (column.widthMode == GridWidthMode::Content &&
                (change.kind == GridModelChange::Kind::Structure || change.cell.column == column.id))
                column.width = ContentWidth(column);
        if (change.kind == GridModelChange::Kind::Structure || change.cell.column == sortColumn)
            Rebuild();
        owner.Invalidate(change.kind == GridModelChange::Kind::Structure);
    }
    void GridView::Impl::CancelDrag()
    {
        std::optional<GridEditEvent> canceled;
        if (drag == Drag::Value)
            canceled = GridEditEvent{valueCell, valueStart, EditPhase::Cancel};
        if (drag == Drag::Resize)
            if (auto* c = Column(dragColumn))
            {
                const float old = c->width;
                c->width = originalWidth;
                c->widthMode = originalMode;
                GridColumnEvent event;
                event.column = c->id;
                event.oldWidth = old;
                event.newWidth = originalWidth;
                event.phase = EditPhase::Cancel;
                owner.OnColumnResized.Raise(event);
            }
        drag = Drag::None;
        moved = false;
        dragColumn.clear();
        pendingClick.reset();
        owner.ReleaseCapture();
        owner.cursor.reset();
        owner.Invalidate(true);
        if (canceled)
            owner.OnEdit.Raise(*canceled);
    }
    GridView::GridView() : impl_(std::make_unique<Impl>(*this))
    {
        impl_->vertical = &Emplace<ScrollBar>();
        impl_->horizontal = &Emplace<ScrollBar>();
        impl_->horizontal->SetOrientation(Orientation::Horizontal);
        impl_->verticalConnection = impl_->vertical->OnEdit.Connect(
            [this](double f, EditPhase)
            { SetScrollOffset(impl_->offset.width, float(f) * impl_->layout.VerticalLimit()); });
        impl_->horizontalConnection = impl_->horizontal->OnEdit.Connect(
            [this](double f, EditPhase)
            { SetScrollOffset(float(f) * impl_->layout.horizontalExtent, impl_->offset.height); });
        SetContextMenuProvider([](Control& c, const ContextMenuRequest& r)
                               { return static_cast<GridView&>(c).impl_->Menu(r); });
    }
    GridView::~GridView()
    {
        impl_->modelConnection.Disconnect();
        impl_->editConnection.Disconnect();
    }
    std::shared_ptr<GridModel> GridView::Model() const
    {
        return impl_->model;
    }
    const std::vector<GridColumn>& GridView::Columns() const
    {
        return impl_->columns;
    }
    const GridOptions& GridView::Options() const
    {
        return impl_->options;
    }
    const GridAppearance& GridView::Appearance() const
    {
        return impl_->appearance;
    }
    void GridView::SetModel(std::shared_ptr<GridModel> model)
    {
        FinishEdit(EditPhase::Cancel);
        impl_->CancelDrag();
        OnPointerLeave();
        impl_->modelConnection.Disconnect();
        impl_->model = std::move(model);
        impl_->sortColumn.clear();
        impl_->direction = GridSortDirection::None;
        impl_->selection.current.reset();
        impl_->selection.anchor.reset();
        ClearSelection();
        OnCurrentCellChanged.Raise(impl_->selection.current);
        impl_->offset = {};
        impl_->columns.clear();
        if (impl_->model)
        {
            for (const auto& c : impl_->model->Columns())
            {
                GridColumn column;
                column.id = column.header = c.id;
                impl_->columns.push_back(std::move(column));
            }
            impl_->modelConnection = impl_->model->OnChange.Connect([this](const GridModelChange& c)
                                                                    { impl_->ModelChanged(c); });
        }
        impl_->Rebuild();
        Invalidate(true);
    }
    void GridView::SetColumns(std::vector<GridColumn> columns)
    {
        std::unordered_set<std::string> ids;
        for (const auto& c : columns)
        {
            if (c.id.empty() || !ids.insert(c.id).second || (impl_->model && !impl_->Definition(c.id)))
                throw std::invalid_argument("Unknown or duplicate grid column");
            if (!std::isfinite(c.width) || !std::isfinite(c.minimumWidth) || !std::isfinite(c.maximumWidth) ||
                !std::isfinite(c.weight) || c.minimumWidth <= 0 || c.maximumWidth < c.minimumWidth || c.weight <= 0)
                throw std::invalid_argument("Invalid grid column dimensions");
        }
        FinishEdit(EditPhase::Cancel);
        impl_->CancelDrag();
        std::stable_sort(columns.begin(), columns.end(),
                         [](const auto& a, const auto& b)
                         {
                             const auto rank = [](GridPin pin)
                             {
                                 return pin == GridPin::Left ? 0 : pin == GridPin::None ? 1 : 2;
                             };
                             return rank(a.pin) < rank(b.pin);
                         });
        impl_->columns = std::move(columns);
        impl_->SizeContentColumns();
        if (!impl_->Column(impl_->sortColumn))
        {
            impl_->sortColumn.clear();
            impl_->direction = GridSortDirection::None;
        }
        if (impl_->selection.current && !impl_->Valid(*impl_->selection.current))
        {
            impl_->selection.current.reset();
            impl_->selection.anchor.reset();
            OnCurrentCellChanged.Raise(impl_->selection.current);
        }
        auto selected = impl_->selection.cells;
        std::erase_if(selected, [&](const GridCell& c) { return !c.column.empty() && !impl_->Valid(c); });
        impl_->Publish(impl_->selection.Replace(std::move(selected)));
        impl_->Rebuild();
        Invalidate(true);
    }
    void GridView::SetOptions(GridOptions options)
    {
        if (!std::isfinite(options.rowHeight) || !std::isfinite(options.headerHeight) || options.rowHeight <= 0 ||
            options.headerHeight <= 0)
            throw std::invalid_argument("Grid heights must be finite and positive");
        const auto old = impl_->options;
        if (options.readOnly || !options.editing || old.singleClickEdit != options.singleClickEdit)
            FinishEdit(EditPhase::Cancel);
        impl_->CancelDrag();
        if (old.stickyHeaders != options.stickyHeaders || old.headerVisible != options.headerVisible)
        {
            const float oldHeader = old.headerVisible && !old.stickyHeaders ? old.headerHeight : 0;
            const float newHeader = options.headerVisible && !options.stickyHeaders ? options.headerHeight : 0;
            if (impl_->offset.height > 0)
                impl_->offset.height = std::max(0.f, impl_->offset.height - oldHeader) + newHeader;
        }
        impl_->options = std::move(options);
        if (!impl_->options.selection)
            ClearSelection();
        else if (old.fullRowSelect != impl_->options.fullRowSelect)
        {
            decltype(impl_->selection.cells) next;
            if (impl_->selection.current)
            {
                auto cell = *impl_->selection.current;
                if (impl_->options.fullRowSelect)
                    cell.column.clear();
                next.insert(cell);
            }
            impl_->Publish(impl_->selection.Replace(std::move(next)));
        }
        else if (!impl_->options.multipleSelection && impl_->selection.cells.size() > 1)
        {
            auto retained = Selection().front();
            if (impl_->selection.current && IsSelected(*impl_->selection.current))
            {
                retained = *impl_->selection.current;
                if (impl_->options.fullRowSelect)
                    retained.column.clear();
            }
            impl_->Publish(impl_->selection.Replace({retained}));
        }
        if (!impl_->options.sorting && impl_->direction != GridSortDirection::None)
            Sort({}, GridSortDirection::None);
        Invalidate(true);
    }
    void GridView::SetAppearance(GridAppearance appearance)
    {
        if (!std::isfinite(appearance.headerPadding) || !std::isfinite(appearance.cellPadding) ||
            !std::isfinite(appearance.borderWidth) || appearance.headerPadding < 0 || appearance.cellPadding < 0 ||
            appearance.borderWidth < 0 ||
            (appearance.headerFont &&
             (!std::isfinite(appearance.headerFont->size) || appearance.headerFont->size <= 0)))
            throw std::invalid_argument("Invalid grid appearance dimensions");
        impl_->appearance = std::move(appearance);
        impl_->SizeContentColumns();
        Invalidate(true);
    }
    bool GridView::SetColumnVisible(const GridColumnId& id, bool visible)
    {
        auto* c = impl_->Column(id);
        if (!c || !FinishEdit(EditPhase::Commit))
            return false;
        impl_->CancelDrag();
        c->visible = visible;
        if (!visible && impl_->selection.current && impl_->selection.current->column == id)
        {
            impl_->selection.current.reset();
            impl_->selection.anchor.reset();
            OnCurrentCellChanged.Raise(impl_->selection.current);
        }
        if (!visible && !impl_->options.fullRowSelect)
        {
            auto selected = impl_->selection.cells;
            std::erase_if(selected, [&](const GridCell& cell) { return cell.column == id; });
            impl_->Publish(impl_->selection.Replace(std::move(selected)));
        }
        Invalidate(true);
        return true;
    }
    bool GridView::PinColumn(const GridColumnId& id, GridPin pin)
    {
        auto* c = impl_->Column(id);
        if (!c || !FinishEdit(EditPhase::Commit))
            return false;
        if (c->pin == pin)
            return true;
        GridColumnEvent event;
        event.column = id;
        event.oldPin = c->pin;
        event.newPin = pin;
        OnColumnPinning.Raise(event);
        if (event.cancel)
            return false;
        impl_->CancelDrag();
        c->pin = pin;
        std::stable_sort(impl_->columns.begin(), impl_->columns.end(),
                         [](const auto& a, const auto& b)
                         {
                             const auto rank = [](GridPin p)
                             {
                                 return p == GridPin::Left ? 0 : p == GridPin::None ? 1 : 2;
                             };
                             return rank(a.pin) < rank(b.pin);
                         });
        Invalidate(true);
        OnColumnPinned.Raise(event);
        return true;
    }
    bool GridView::MoveColumn(const GridColumnId& id, size_t index)
    {
        auto* c = impl_->Column(id);
        if (!c || index >= impl_->columns.size() || !impl_->options.reordering || !c->reorderable ||
            c->pin != impl_->columns[index].pin || !FinishEdit(EditPhase::Commit))
            return false;
        const auto old = size_t(c - impl_->columns.data());
        if (old == index)
            return true;
        GridColumnEvent event;
        event.column = id;
        event.oldIndex = old;
        event.newIndex = index;
        OnColumnReordering.Raise(event);
        if (event.cancel)
            return false;
        auto moved = std::move(*c);
        impl_->columns.erase(impl_->columns.begin() + old);
        impl_->columns.insert(impl_->columns.begin() + index, std::move(moved));
        Invalidate(true);
        OnColumnReordered.Raise(event);
        return true;
    }
    bool GridView::ResizeColumn(const GridColumnId& id, float width)
    {
        auto* c = impl_->Column(id);
        if (!c || !std::isfinite(width) || !impl_->options.resizing || !c->resizable || !FinishEdit(EditPhase::Commit))
            return false;
        GridColumnEvent event;
        event.column = id;
        event.oldWidth = c->width;
        event.newWidth = std::clamp(width, c->minimumWidth, c->maximumWidth);
        event.phase = impl_->drag == Impl::Drag::Resize ? EditPhase::Preview : EditPhase::Commit;
        OnColumnResizing.Raise(event);
        if (event.cancel || !std::isfinite(event.newWidth))
            return false;
        event.newWidth = std::clamp(event.newWidth, c->minimumWidth, c->maximumWidth);
        c->width = event.newWidth;
        c->widthMode = GridWidthMode::Fixed;
        Invalidate(true);
        OnColumnResized.Raise(event);
        return true;
    }
    bool GridView::AutoSizeColumn(const GridColumnId& id)
    {
        auto* c = impl_->Column(id);
        const auto* definition = impl_->Definition(id);
        if (!c || !definition || !impl_->options.resizing || !c->resizable || !FinishEdit(EditPhase::Commit))
            return false;
        return ResizeColumn(id, impl_->ContentWidth(*c));
    }
    bool GridView::Sort(const GridColumnId& id, GridSortDirection direction)
    {
        auto* c = impl_->Column(id);
        if (!impl_->model ||
            (direction != GridSortDirection::None && (!c || !c->sortable || !impl_->options.sorting)) ||
            !FinishEdit(EditPhase::Commit))
            return false;
        GridColumnEvent event;
        event.column = id;
        event.direction = direction;
        OnSorting.Raise(event);
        if (event.cancel)
            return false;
        impl_->sortColumn = direction == GridSortDirection::None ? GridColumnId{} : id;
        impl_->direction = direction;
        impl_->Rebuild();
        Invalidate(true);
        OnSorted.Raise(event);
        return true;
    }
    GridColumnId GridView::SortColumn() const
    {
        return impl_->sortColumn;
    }
    GridSortDirection GridView::SortDirection() const
    {
        return impl_->direction;
    }
    GridRowId GridView::DisplayRowId(size_t index) const
    {
        if (!impl_->model)
            throw std::out_of_range("Grid has no model");
        return impl_->projection.Row(*impl_->model, index);
    }
    std::optional<GridCell> GridView::CurrentCell() const
    {
        return impl_->selection.current;
    }
    bool GridView::IsSelected(const GridCell& cell) const
    {
        return impl_->selection.Selected(cell, impl_->options.fullRowSelect);
    }
    std::vector<GridCell> GridView::Selection() const
    {
        std::vector<GridCell> result(impl_->selection.cells.begin(), impl_->selection.cells.end());
        std::sort(result.begin(), result.end(),
                  [&](const auto& a, const auto& b)
                  {
                      const auto x = impl_->projection.Index(*impl_->model, a.row).value_or(0),
                                 y = impl_->projection.Index(*impl_->model, b.row).value_or(0);
                      if (x != y)
                          return x < y;
                      if (a.column.empty() || b.column.empty())
                          return a.column < b.column;
                      return impl_->Column(a.column) < impl_->Column(b.column);
                  });
        return result;
    }
    void GridView::ClearSelection()
    {
        impl_->Publish(impl_->selection.Replace({}));
    }
    bool GridView::SelectCell(GridCell cell, bool extend, bool toggle, bool preserve)
    {
        return SelectCellCore(std::move(cell), extend, toggle, preserve, true);
    }
    bool GridView::SelectCellCore(GridCell cell, bool extend, bool toggle, bool preserve, bool reveal)
    {
        if (!impl_->Valid(cell))
            return false;
        if (impl_->selection.current != cell && !FinishEdit(EditPhase::Commit))
            return false;
        const auto oldCurrent = impl_->selection.current;
        impl_->selection.current = cell;
        GridSelectionChange delta;
        if (impl_->options.selection && !preserve)
        {
            auto next = toggle && impl_->options.multipleSelection ? impl_->selection.cells
                                                                   : decltype(impl_->selection.cells){};
            if (extend && impl_->options.multipleSelection && impl_->selection.anchor &&
                impl_->Valid(*impl_->selection.anchor))
            {
                if (toggle)
                    next = impl_->selection.cells;
                const auto start = *impl_->projection.Index(*impl_->model, impl_->selection.anchor->row);
                const auto end = *impl_->projection.Index(*impl_->model, cell.row);
                const auto* first = impl_->Column(impl_->selection.anchor->column);
                const auto* last = impl_->Column(cell.column);
                const size_t left = size_t(std::min(first, last) - impl_->columns.data()),
                             right = size_t(std::max(first, last) - impl_->columns.data());
                for (size_t row = std::min(start, end); row <= std::max(start, end); ++row)
                    if (impl_->options.fullRowSelect)
                        next.insert({DisplayRowId(row), {}});
                    else
                        for (size_t col = left; col <= right; ++col)
                            if (impl_->columns[col].visible)
                                next.insert({DisplayRowId(row), impl_->columns[col].id});
            }
            else
            {
                impl_->selection.anchor = cell;
                GridCell selected = cell;
                if (impl_->options.fullRowSelect)
                    selected.column.clear();
                if (toggle && impl_->options.multipleSelection && next.contains(selected))
                    next.erase(selected);
                else
                    next.insert(selected);
            }
            delta = impl_->selection.Replace(std::move(next));
        }
        if (oldCurrent != impl_->selection.current)
            OnCurrentCellChanged.Raise(impl_->selection.current);
        impl_->Publish(std::move(delta));
        if (reveal)
            impl_->Reveal(cell);
        Invalidate();
        return true;
    }
    void GridView::SelectAll()
    {
        if (!impl_->model || !impl_->options.selection || !impl_->options.multipleSelection ||
            !impl_->options.selectAll || !FinishEdit(EditPhase::Commit))
            return;
        decltype(impl_->selection.cells) next;
        for (size_t i = 0; i < impl_->model->RowCount(); ++i)
            if (impl_->options.fullRowSelect)
                next.insert({impl_->model->RowId(i), {}});
            else
                for (const auto& c : impl_->columns)
                    if (c.visible)
                        next.insert({impl_->model->RowId(i), c.id});
        impl_->Publish(impl_->selection.Replace(std::move(next)));
    }
    bool GridView::BeginEdit(const GridCell& cell)
    {
        if (!Host() && !impl_->finishing)
            impl_->Cleanup();
        if (!impl_->Editable(cell))
            return false;
        if (impl_->editor && impl_->editCell == cell)
            return true;
        if (!FinishEdit(EditPhase::Commit))
            return false;
        GridEditRequest request{cell};
        OnEditBeginning.Raise(request);
        if (request.cancel)
            return false;
        if (!SelectCell(cell))
            return false;
        const auto* c = impl_->Column(cell.column);
        const auto* definition = impl_->Definition(cell.column);
        auto value = impl_->model->Value(cell);
        auto editor = c->editor ? c->editor(*definition, value) : internal::CreateGridEditor(*definition, value);
        if (!editor)
            return false;
        impl_->editCell = cell;
        impl_->editStart = value;
        impl_->editor = editor.get();
        Add(std::move(editor));
        impl_->editConnection = impl_->editor->OnEdit.Connect(
            [this](EditPhase phase)
            {
                if (impl_->finishing || !impl_->editor)
                    return;
                if (phase == EditPhase::Preview)
                {
                    impl_->editor->SetValidation({});
                    auto draft = impl_->editor->Draft();
                    if (!draft)
                    {
                        impl_->Error(draft.error());
                        return;
                    }
                    OnEdit.Raise({impl_->editCell, *draft, phase});
                }
                else
                    FinishEdit(phase);
            });
        OnArrange();
        if (!impl_->editor)
            return false;
        if (Host())
        {
            impl_->focusTransition = true;
            const bool focused = Host()->Focus(&impl_->editor->FocusTarget());
            impl_->focusTransition = false;
            if (!focused)
            {
                FinishEdit(EditPhase::Cancel);
                return false;
            }
        }
        Invalidate();
        return true;
    }
    bool GridView::FinishEdit(EditPhase phase)
    {
        if (impl_->drag == Impl::Drag::Value)
            return impl_->FinishValueDrag(phase);
        if (!impl_->editor || impl_->finishing || impl_->focusTransition)
            return true;
        const internal::ScopedFlag guard(impl_->finishing, true);
        if (phase == EditPhase::Preview)
            return true;
        if (Host() && Host()->HasPopupWithin(*impl_->editor))
        {
            Host()->ClosePopup(phase);
            if (Host()->HasPopupWithin(*impl_->editor))
                return false;
        }
        if (phase == EditPhase::Cancel)
        {
            const GridEditEvent event{impl_->editCell, impl_->editStart, phase};
            impl_->RetireEditor();
            OnEdit.Raise(event);
            return true;
        }
        auto draft = impl_->editor->Draft();
        if (!draft)
            return impl_->Error(draft.error());
        GridValueChange change{impl_->editCell, impl_->model->Value(impl_->editCell), *draft};
        if (auto status = impl_->RequestChange(change, true); !status)
        {
            if (!impl_->Editable(impl_->editCell))
            {
                const GridEditEvent canceled{impl_->editCell, impl_->editStart, EditPhase::Cancel};
                impl_->RetireEditor();
                OnEdit.Raise(canceled);
            }
            return impl_->Error(status.error());
        }
        impl_->editor->SetValidation({});
        if (!impl_->editor->Finish(EditPhase::Commit))
            return impl_->Error("Editor could not commit its draft");
        if (change.oldValue != change.newValue)
        {
            auto status = impl_->StoreChange(change);
            if (!status)
                return impl_->Error(status.error());
        }
        const GridEditEvent edit{impl_->editCell, impl_->model->Value(impl_->editCell), phase};
        change.newValue = edit.value;
        impl_->RetireEditor();
        impl_->PublishChange(change, true);
        OnEdit.Raise(edit);
        Invalidate();
        return true;
    }
    bool GridView::Editing() const
    {
        return impl_->editor != nullptr || impl_->drag == Impl::Drag::Value;
    }
    GridCellEditor* GridView::ActiveEditor() const
    {
        return impl_->editor;
    }
    GridStatus GridView::SetCellValue(const GridCell& cell, const GridValue& value, bool notify)
    {
        if (!impl_->model || !impl_->model->IndexOf(cell.row) || !impl_->Definition(cell.column))
            return std::unexpected("Unknown grid cell");
        GridValueChange change{cell, impl_->model->Value(cell), value, GridOrigin::Programmatic};
        if (auto status = impl_->RequestChange(change, notify); !status)
            return status;
        if (auto status = impl_->StoreChange(change); !status)
            return status;
        if ((impl_->editor && impl_->editCell == cell) ||
            (impl_->drag == Impl::Drag::Value && impl_->valueCell == cell))
            FinishEdit(EditPhase::Cancel);
        impl_->PublishChange(change, notify);
        return {};
    }
    std::string GridView::CopyText() const
    {
        if (!impl_->model || !impl_->options.copy || impl_->selection.cells.empty())
            return {};
        std::unordered_set<GridColumnId> needed;
        for (const auto& cell : impl_->selection.cells)
            needed.insert(cell.column);
        std::vector<const GridColumn*> columns;
        for (const auto& column : impl_->columns)
            if (column.visible && (impl_->options.fullRowSelect || needed.contains(column.id)))
                columns.push_back(&column);
        std::string text;
        bool firstRow = true;
        const auto append = [&](std::string row)
        {
            if (!firstRow)
                text += "\r\n";
            firstRow = false;
            text += row;
        };
        if (impl_->options.copyHeaders)
        {
            std::string header;
            for (size_t i = 0; i < columns.size(); ++i)
            {
                if (i)
                    header += '\t';
                header += internal::ClipboardField(columns[i]->header);
            }
            append(header);
        }
        std::vector<std::pair<size_t, GridRowId>> rows;
        std::unordered_set<GridRowId> seen;
        for (const auto& cell : impl_->selection.cells)
            if (seen.insert(cell.row).second)
                if (auto index = impl_->projection.Index(*impl_->model, cell.row))
                    rows.push_back({*index, cell.row});
        std::sort(rows.begin(), rows.end());
        for (const auto& [index, id] : rows)
        {
            std::string line;
            for (size_t i = 0; i < columns.size(); ++i)
            {
                if (i)
                    line += '\t';
                const auto& column = *columns[i];
                if (impl_->options.fullRowSelect || impl_->selection.cells.contains({id, column.id}))
                    line += internal::ClipboardField(internal::GridText(column, *impl_->Definition(column.id),
                                                                        impl_->model->Value({id, column.id})));
            }
            append(line);
        }
        return text;
    }
    bool GridView::CopySelection()
    {
        if (!Host() || !impl_->options.copy || impl_->selection.cells.empty())
            return false;
        return LWS::SetClipboardText(Host()->Window(), CopyText()) == LWS::ClipboardResult::Success;
    }
    bool GridView::Finish(EditPhase phase)
    {
        if (!FinishEdit(phase))
            return false;
        if (phase == EditPhase::Cancel)
            impl_->CancelDrag();
        return true;
    }
    bool GridView::PrepareFocusLoss(Control* next, EditPhase phase)
    {
        bool inside = false;
        for (auto* c = next; c; c = c->Parent())
            if (c == this)
                inside = true;
        if (!inside && impl_->drag != Impl::Drag::None)
            impl_->CancelDrag();
        if (!impl_->editor || impl_->focusTransition)
            return true;
        for (auto* c = next; c; c = c->Parent())
            if (c == impl_->editor)
                return true;
        return FinishEdit(phase);
    }
    void GridView::OnAttach()
    {
        impl_->SizeContentColumns();
        if (!impl_->retired.empty())
            Host()->Post(Handle(), [](Control& control) { static_cast<GridView&>(control).impl_->Cleanup(); });
    }
    void GridView::OnDetach()
    {
        FinishEdit(EditPhase::Cancel);
        impl_->CancelDrag();
        OnPointerLeave();
    }
}  // namespace LWSUI
