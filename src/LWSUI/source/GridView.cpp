#include "GridInternals.hpp"
#include <LWSUI/UIHost.hpp>
#include <LWS/TextClipboard.hpp>
#include <cmath>
#include <stdexcept>
namespace LWSUI
{
    struct GridView::Impl
    {
        GridView& owner;
        std::shared_ptr<GridModel> model;
        std::vector<GridColumn> columns;
        GridOptions options;
        GridAppearance appearance;
        internal::GridLayout layout;
        internal::GridProjection projection;
        internal::GridSelection selection;
        GridColumnId sortColumn;
        GridSortDirection direction = GridSortDirection::None;
        Size offset;
        ScrollBar *vertical = nullptr, *horizontal = nullptr;
        GridCellEditor* editor = nullptr;
        GridCell editCell;
        GridValue editStart;
        std::optional<GridCell> hovered;
        std::vector<GridCellEditor*> retired;
        Event<void(const GridModelChange&)>::Connection modelConnection;
        Event<void(double, EditPhase)>::Connection verticalConnection, horizontalConnection;
        Event<void(EditPhase)>::Connection editConnection;
        bool finishing = false, applying = false, focusTransition = false;
        enum class Drag
        {
            None,
            Header,
            Resize,
            Rows,
            Cell,
            Value
        };
        Drag drag = Drag::None;
        GridColumnId dragColumn;
        float dragStart = 0, originalWidth = 0, dragWidth = 0, pointerX = 0, pointerY = -1;
        GridWidthMode originalMode = GridWidthMode::Fixed;
        Size arrangedSize;
        bool moved = false;
        std::optional<GridCell> pendingClick;
        GridCell pressedCell;
        Rect pressedCellBounds;
        GridCell valueCell;
        GridValue valueStart, valueDraft;
        float pressX = 0, pressY = 0;
        Impl(GridView& grid) : owner(grid) {}
        GridColumn* Column(const GridColumnId& id)
        {
            auto it = std::ranges::find(columns, id, &GridColumn::id);
            return it == columns.end() ? nullptr : &*it;
        }
        const GridModelColumn* Definition(const GridColumnId& id) const
        {
            if (!model)
                return nullptr;
            auto definitions = model->Columns();
            auto it = std::ranges::find(definitions, id, &GridModelColumn::id);
            return it == definitions.end() ? nullptr : &*it;
        }
        bool Valid(const GridCell& cell) const
        {
            return model && model->IndexOf(cell.row) && Definition(cell.column) &&
                   std::ranges::any_of(columns, [&](const GridColumn& c) { return c.id == cell.column && c.visible; });
        }
        bool Editable(const GridCell& cell)
        {
            auto* c = Column(cell.column);
            return Valid(cell) && options.editing && !options.readOnly && c && c->editable && model->Editable(cell);
        }
        float ContentWidth(const GridColumn& column) const
        {
            const auto* definition = Definition(column.id);
            if (!definition)
                return column.width;
            const auto measure = [&](const std::string& text, const FontSpec& font)
            {
                return owner.Host() ? owner.Host()->MeasureText(text, 100000.f, false, font).width
                                    : float(text.size()) * font.size * .65f;
            };
            float width = measure(column.header, appearance.headerFont.value_or(owner.Font())) +
                          2 * appearance.headerPadding + 18;
            for (size_t i = 0; i < model->RowCount(); ++i)
                width = std::max(width, measure(internal::GridText(column, *definition,
                                                                   model->Value({model->RowId(i), column.id})),
                                                owner.Font()) +
                                            2 * appearance.cellPadding);
            return std::clamp(width, column.minimumWidth, column.maximumWidth);
        }
        void SizeContentColumns()
        {
            for (auto& column : columns)
                if (column.widthMode == GridWidthMode::Content)
                    column.width = ContentWidth(column);
        }
        void Rebuild()
        {
            if (model)
                projection.Rebuild(*model, Column(sortColumn), direction);
        }
        void Publish(GridSelectionChange change)
        {
            if (!change.added.empty() || !change.removed.empty())
                owner.OnSelectionChanged.Raise(change);
            owner.Invalidate();
        }
        GridStatus RequestChange(GridValueChange& change, bool notify)
        {
            if (notify && change.oldValue != change.newValue)
                owner.OnCellValueChanging.Raise(change);
            if (change.cancel)
                return std::unexpected(change.error.empty() ? "Cell change was rejected" : change.error);
            return {};
        }
        GridStatus StoreChange(GridValueChange& change)
        {
            struct Restore
            {
                bool& flag;
                bool previous;
                ~Restore() { flag = previous; }
            } restore{applying, applying};
            applying = applying || change.origin == GridOrigin::User;
            auto status = model->SetValue(change.cell, change.newValue);
            if (status)
                change.newValue = model->Value(change.cell);
            return status;
        }
        void PublishChange(const GridValueChange& change, bool notify)
        {
            if (notify && change.oldValue != change.newValue)
                owner.OnCellValueChanged.Raise(change);
            owner.Invalidate();
        }
        bool ActivateClick(const GridCell& cell, const Input& input)
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
            if (status && !Editable(cell))
                status = std::unexpected("Cell is read-only");
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
        bool BeginValueDrag(const GridCell& cell, const Input& input, Rect bounds)
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
        void UpdateValueDrag(const Input& input)
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
        bool FinishValueDrag(EditPhase phase)
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
            struct Guard
            {
                bool& flag;
                Guard(bool& f) : flag(f) { flag = true; }
                ~Guard() { flag = false; }
            } guard(finishing);
            const auto cell = valueCell;
            GridValueChange change{cell, valueStart, valueDraft, GridOrigin::User};
            auto status = RequestChange(change, true);
            if (drag != Drag::Value)
                return false;
            if (status && !Editable(cell))
                status = std::unexpected("Cell is read-only");
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
        void RetireEditor()
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
        void Cleanup()
        {
            auto handles = std::move(retired);
            retired.clear();
            for (auto* c : handles)
                if (std::ranges::any_of(owner.Children(), [c](const auto& child) { return child.get() == c; }))
                    owner.Remove(*c);
        }
        bool Error(std::string message)
        {
            if (editor)
                editor->SetValidation(message);
            owner.OnValidationFailed.Raise({editCell, std::move(message)});
            owner.Invalidate();
            return false;
        }
        void ModelChanged(const GridModelChange& change)
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
        void CancelDrag()
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
        void Reveal(const GridCell& cell)
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
        std::vector<ContextMenuItem> Menu(const ContextMenuRequest& request)
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
    };
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
        struct Guard
        {
            bool& flag;
            Guard(bool& f) : flag(f) { flag = true; }
            ~Guard() { flag = false; }
        } guard(impl_->finishing);
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
            return impl_->Error(status.error());
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
        float width = canvas.Measure(text, font), x = b.x;
        if (alignment == Alignment::Center)
            x += std::max(0.f, (b.width - width) / 2);
        if (alignment == Alignment::End)
            x += std::max(0.f, b.width - width);
        canvas.Text(text, x, b.y + std::max(0.f, (b.height - font.size * 1.4f) / 2), b.width - (x - b.x), b.height,
                    color, font);
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
                            a.headerFont.value_or(theme.headingFont));
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
    bool GridView::OnInput(const Input& input)
    {
        using K = LWS::KeyCode;
        if (input.kind == InputKind::Cancel)
            return Finish(EditPhase::Cancel);
        if (input.kind == InputKind::Wheel)
        {
            SetScrollOffset(impl_->offset.width - (input.shift ? input.wheel * Style().scrollStep : 0),
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
        if (input.control && input.key == K::C)
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
