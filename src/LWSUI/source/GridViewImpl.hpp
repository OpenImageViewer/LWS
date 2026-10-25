#pragma once
#include "GridInternals.hpp"
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
        GridScrollOffset offset;
        ScrollBar *vertical = nullptr, *horizontal = nullptr;
        GridCellEditor* editor = nullptr;
        GridCell editCell;
        GridValue editStart;
        std::optional<GridCell> hovered;
        std::vector<GridCellEditor*> retired;
        Event<void(const GridModelChange&)>::Connection modelConnection;
        Event<void(double, EditPhase)>::Connection verticalConnection, horizontalConnection;
        Event<void(EditPhase)>::Connection editConnection;
        Event<void(double)>::Connection verticalSpeed, horizontalSpeed;
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
        Impl(GridView& grid);
        GridColumn* Column(const GridColumnId& id);
        const GridModelColumn* Definition(const GridColumnId& id) const;
        bool Valid(const GridCell& cell) const;
        bool Editable(const GridCell& cell);
        FontSpec HeaderFont() const;
        float ContentWidth(const GridColumn& column) const;
        void SizeContentColumns();
        void Rebuild();
        void Publish(GridSelectionChange change);
        GridStatus RequestChange(GridValueChange& change, bool notify);
        GridStatus StoreChange(GridValueChange& change);
        void PublishChange(const GridValueChange& change, bool notify);
        bool ActivateClick(const GridCell& cell, const Input& input);
        bool BeginValueDrag(const GridCell& cell, const Input& input, Rect bounds);
        void UpdateValueDrag(const Input& input);
        bool FinishValueDrag(EditPhase phase);
        void RetireEditor();
        void Cleanup();
        bool Error(std::string message);
        void ModelChanged(const GridModelChange& change);
        void CancelDrag();
        void Reveal(const GridCell& cell);
        std::vector<ContextMenuItem> Menu(const ContextMenuRequest& request);
    };
}  // namespace LWSUI
