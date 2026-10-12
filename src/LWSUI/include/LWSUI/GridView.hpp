#pragma once
#include <LWSUI/GridModel.hpp>
#include <LWSUI/Containers.hpp>
namespace LWSUI
{
    // Future TODOs and references:
    // - Filtering and multiple-column sorting, informed by Dear ImGui tables.
    // - Saved layouts, informed by Qt QHeaderView.
    // - Bulk paste, undo/redo, grouping, summaries, and expandable details, informed by Avalonia DataGrid.
    // - Variable row heights, wrapping, merged cells, sticky rows, and row headers, informed by Qt QTableView and
    // wxWidgets grid.
    // - Accessibility integration and asynchronous paging as separate host/backend and model extensions.
    //   Accessibility consumes grid cell identity, selection and editability through host/backend integration.
    // Features deferred beyond the initial GridView implementation:
    // - Filtering and advanced filtering expressions
    // - Search and find
    // - Row grouping
    // - Row expansion and tree hierarchy
    // - Persistent column layout
    // - Drag-and-drop rows
    // - Built-in action-button cells
    // - Aggregation and summary rows
    // - Incremental and lazy data loading
    // - Multi-column sorting
    // - Column grouping and multi-level headers
    // - Pivoting
    // - Spreadsheet-style formulas
    // - Cell merging
    // - Conditional formatting rules
    // - Ellipsis and full-value tooltips for clipped cells and headers
    // - Visible validation messages beside active editors
    // - Edge auto-scrolling during selection and column dragging, including stationary pointers
    // - Configurable Enter navigation with Shift reversing direction
    // - Visible-row or bounded-sample auto-sizing alongside explicit full-data sizing
    // - Column chooser and reset-layout action
    // - Distinct current-cell, selected, inactive, editable and read-only states
    // - Host/demo status feedback for selection count, current position and copying
    // - Stable visible-row anchoring during model updates and explicit sorting scroll behavior
    // - Contextual demo instructions and explanations of dependent settings
    enum class GridPin
    {
        None,
        Left,
        Right
    };
    enum class GridWidthMode
    {
        Fixed,
        Content,
        Proportional
    };
    enum class GridSortDirection
    {
        None,
        Ascending,
        Descending
    };
    enum class GridOrigin
    {
        User,
        Programmatic
    };
    struct GridScrollOffset
    {
        double width = 0, height = 0;
    };
    struct GridOptions
    {
        bool selection = true, fullRowSelect = true, multipleSelection = false;
        bool editing = true, readOnly = false, sorting = true, reordering = true, resizing = true;
        bool headerVisible = true, stickyHeaders = true, sortGlyphs = true, striped = false;
        bool horizontalLines = true, verticalLines = true, copy = true, copyHeaders = false;
        bool singleClickEdit = false, doubleClickEdit = true, f2Edit = true, typingEdit = true;
        bool selectAll = true, tabNavigation = true, contextMenus = true, dragEditing = true;
        bool dynamicResolution = false, resolutionGuide = false;
        ScrollResolution centerResolution = ScrollResolution::Row;
        float rowHeight = 34, headerHeight = 36;
        std::string emptyText = "No rows";
        // Read-only, pinned column showing one-based positions in the current display order.
        bool lineNumbers = false;
        // Header text for the presentation-only line-number column, empty is allowed.
        std::string lineNumberLabel = "Line";
        // Inward fraction of the full perpendicular control dimension, in (0, 1].
        double inwardDistance = .5;
    };
    struct GridAppearance
    {
        std::optional<LLUtils::Color> headerBackground, headerForeground, headerHover, headerPressed, headerBorder;
        std::optional<LLUtils::Color> alternateRow, cellBackground, cellForeground, selected, hovered, line, focus;
        std::optional<FontSpec> headerFont;
        float headerPadding = 8, cellPadding = 6, borderWidth = 1;
        Alignment headerAlignment = Alignment::Start;
    };
    struct GridCellPresentation
    {
        GridCell cell;
        GridValue value;
        Rect bounds;
        std::string text;
        bool selected = false, hovered = false, current = false;
    };
    // Owns its field tree. Finish must validate drafts before committing.
    // OnEdit reports draft activity, the grid owns the authoritative transaction.
    class GridCellEditor : public Container
    {
      public:

        virtual std::expected<GridValue, std::string> Draft() const = 0;
        virtual void SetValidation(std::string) = 0;
        virtual Control& FocusTarget() = 0;
        Event<void(EditPhase)> OnEdit;
    };
    struct GridColumn
    {
        GridColumnId id;
        std::string header;
        GridWidthMode widthMode = GridWidthMode::Fixed;
        float width = 160, minimumWidth = 40, maximumWidth = 2000, weight = 1;
        GridPin pin = GridPin::None;
        bool visible = true, editable = true, sortable = true, reorderable = true, resizable = true;
        Alignment alignment = Alignment::Start;
        std::optional<LLUtils::Color> headerBackground, headerForeground;
        std::function<std::string(const GridValue&)> format;
        std::function<int(const GridValue&, const GridValue&)> compare;
        std::function<void(Canvas&, const GridCellPresentation&)> render;
        // Single-click value action for an interactive renderer. Nullopt opens its editor.
        // Bounds describe the cell at pointer press, before focus can reveal another column.
        std::function<std::optional<GridValue>(const GridCellPresentation&, const Input&)> clickValue;
        // Capture pointer motion for clickValue, preview visually, and commit on release.
        bool dragValue = false;
        std::function<std::unique_ptr<GridCellEditor>(const GridModelColumn&, const GridValue&)> editor;
    };
    struct GridCellEvent
    {
        GridCell cell;
        Input input;
    };
    struct GridEditRequest
    {
        GridCell cell;
        bool cancel = false;
    };
    struct GridEditEvent
    {
        GridCell cell;
        GridValue value;
        EditPhase phase = EditPhase::Preview;
    };
    struct GridValueChange
    {
        GridCell cell;
        GridValue oldValue, newValue;
        GridOrigin origin = GridOrigin::User;
        bool cancel = false;
        std::string error;
    };
    struct GridValidationEvent
    {
        GridCell cell;
        std::string message;
    };
    struct GridSelectionChange
    {
        // Empty column identifies a full selected row.
        std::vector<GridCell> added, removed;
    };
    struct GridColumnEvent
    {
        GridColumnId column;
        size_t oldIndex = 0, newIndex = 0;
        float oldWidth = 0, newWidth = 0;
        GridPin oldPin = GridPin::None, newPin = GridPin::None;
        GridSortDirection direction = GridSortDirection::None;
        EditPhase phase = EditPhase::Commit;
        bool cancel = false;
    };
    // Composite implementation owns its children. UI-thread confined.
    // Structural callback changes use UIHost::Post, like other LWSUI controls.
    class GridView : public Container
    {
      public:

        GridView();
        ~GridView() override;
        void SetModel(std::shared_ptr<GridModel>);
        std::shared_ptr<GridModel> Model() const;
        void SetColumns(std::vector<GridColumn>);
        const std::vector<GridColumn>& Columns() const;
        void SetOptions(GridOptions);
        const GridOptions& Options() const;
        void SetAppearance(GridAppearance);
        const GridAppearance& Appearance() const;
        bool SetColumnVisible(const GridColumnId&, bool);
        bool PinColumn(const GridColumnId&, GridPin);
        bool MoveColumn(const GridColumnId&, size_t index);
        bool ResizeColumn(const GridColumnId&, float width);
        bool AutoSizeColumn(const GridColumnId&);
        bool Sort(const GridColumnId&, GridSortDirection);
        GridColumnId SortColumn() const;
        GridSortDirection SortDirection() const;
        GridRowId DisplayRowId(size_t index) const;
        std::optional<GridCell> CurrentCell() const;
        bool SelectCell(GridCell, bool extend = false, bool toggle = false, bool preserve = false);
        std::vector<GridCell> Selection() const;
        bool IsSelected(const GridCell&) const;
        void ClearSelection();
        void SelectAll();
        bool BeginEdit(const GridCell&);
        bool FinishEdit(EditPhase);
        bool Editing() const;
        GridCellEditor* ActiveEditor() const;
        GridStatus SetCellValue(const GridCell&, const GridValue&, bool notify = false);
        std::string CopyText() const;
        bool CopySelection();
        void SetScrollOffset(float x, float y);
        Size ScrollOffset() const;
        // Double precision preserves individual rows and pixels in very large virtual models.
        void SetPreciseScrollOffset(double x, double y);
        GridScrollOffset PreciseScrollOffset() const;
        Event<void(GridScrollOffset)> OnPreciseScroll;
        Event<void(double)> OnScrollSpeed;
        std::optional<Rect> CellBounds(const GridCell&) const;
        std::optional<GridCell> CellAt(float x, float y) const;
        size_t FirstVisibleRow() const;
        size_t VisibleRowCount() const;
        bool Focusable() const override { return true; }
        bool Finish(EditPhase) override;
        bool PrepareFocusLoss(Control*, EditPhase) override;
        Event<void(const GridCellEvent&)> OnCellClick, OnCellDoubleClick, OnCellEnter, OnCellLeave, OnContextRequest;
        Event<void(const std::optional<GridCell>&)> OnCurrentCellChanged;
        Event<void(const GridSelectionChange&)> OnSelectionChanged;
        Event<void(GridEditRequest&)> OnEditBeginning;
        Event<void(const GridEditEvent&)> OnEdit;
        Event<void(GridValueChange&)> OnCellValueChanging;
        Event<void(const GridValueChange&)> OnCellValueChanged;
        Event<void(const GridValidationEvent&)> OnValidationFailed;
        Event<void(GridColumnEvent&)> OnSorting, OnColumnReordering, OnColumnResizing, OnColumnPinning;
        Event<void(const GridColumnEvent&)> OnSorted, OnColumnReordered, OnColumnResized, OnColumnPinned, OnHeaderClick;
        Event<void(Size)> OnScroll;

      protected:

        Size OnMeasure(Size available) override { return available; }
        void OnArrange() override;
        void OnRender(Canvas&) override;
        bool OnInput(const Input&) override;
        bool OnPreviewInput(const Input&) override;
        void OnPointerLeave() override;
        void OnAttach() override;
        void OnDetach() override;

      private:

        using Container::Add;
        using Container::Clear;
        using Container::Emplace;
        using Container::Remove;
        bool SelectCellCore(GridCell, bool extend, bool toggle, bool preserve, bool reveal);
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}  // namespace LWSUI
