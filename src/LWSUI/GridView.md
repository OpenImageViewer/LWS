# GridView

`GridView` is a reusable tabular control in `LWSUI::LWSUI` for Windows and Wayland.
It owns scrollbars and one active cell editor. Its single-column presentation is
also the list-view presentation, sharing all selection and input behavior.

## Minimal use

```cpp
#include <LWSUI/GridView.hpp>

auto model = std::make_shared<LWSUI::MemoryGridModel>(
    std::vector<LWSUI::GridModelColumn>{
        {"name"}, {"quantity", LWSUI::GridValueType::Integer},
        {"enabled", LWSUI::GridValueType::Boolean}});
model->AddRow({std::string("Example"), int64_t(42), true});
auto grid = std::make_unique<LWSUI::GridView>();
auto* view = grid.get();
view->SetModel(model);
auto options = view->Options();
options.fullRowSelect = true;
options.multipleSelection = true;
view->SetOptions(options);
view->PinColumn("name", LWSUI::GridPin::Left);
auto changed = view->OnCellValueChanged.Connect(
    [](const LWSUI::GridValueChange& change) {
        // React to a committed cell change.
    });
host.SetRoot(std::move(grid));
```

Retain event connections for the required subscription lifetime. All model and
control operations run on the UI thread. Structural changes requested inside
control callbacks use `UIHost::Post`, including SetModel and SetColumns.
Model row additions/removals inside callbacks must also be posted.

## Configuration

`SetOptions` controls selection, full-row/multiple selection, editing, read-only,
sorting, resizing, reordering, header/glyph visibility, striping, grid lines,
copying, context menus, keyboard behavior, edit triggers, sticky headers, row/header heights,
and empty-state text. `SetAppearance` controls colors, header typography and
alignment, padding, borders, selection, hover, and focus styling.

`SetColumns` supplies presentation independently of the model. Columns have
stable identities, headers, fixed/content/proportional widths, bounds, alignment,
visibility, left/right pinning, edit/sort/resize/reorder capabilities, formatting,
comparators, renderers, and custom editor factories. Pinned regions never overlap
at narrow widths. They may be clipped when wider than the available space.
Unpin or hide columns to expose scrolling cells in such a viewport.

Content-sized columns measure model data when configured or changed.
Double-click auto-sizing and sorting are explicit full-data operations. Ordinary
layout and rendering of fixed/proportional columns do not scan all model rows.
Headers stay visible during vertical scrolling by default. Set `stickyHeaders`
to false to scroll them with the data, independently of `headerVisible` and
left/right column pinning. Toggling the mode preserves the first visible data row
where possible and clamps scrolling to the new extent.

Scrollbars support both orientations through `SetOrientation`. Vertical remains
the default for existing callers.

## Selection and editing

A click selects a cell or entire row. Ctrl-click toggles, Shift-click selects an
anchored range, and Ctrl+Shift-click adds a range. In row mode, the highlight
continues across both pinned regions. The current cell remains separate from
selected rows and identifies the editor column.

Arrows, Home/End, Page Up/Down and Tab navigate cells. Ctrl+Up/Down moves the current
cell without replacing row selection. Shift+Up/Down extends a range. Ctrl+Space
toggles the current row and Ctrl+A selects all when enabled. Selected identities
survive sorting, while deleted rows leave selection. Disabling multiple selection
retains the current selected item or the first selected item in display order.
Changing cell/row selection mode selects the current cell/row with one delta event.

Double-click, F2 or typing opens an editor when its trigger is enabled.
`singleClickEdit` defaults to false. Enable it to open an editor after a plain
left click is released over its original cell. Pointer selection does not scroll
the target during the gesture. Movement inside the same cell remains a click,
and entering another cell beyond the drag threshold starts range selection.
Single-click mode takes precedence over double-click activation. Changing that
mode cancels the old draft and any pending pointer gesture.
Modifier clicks and selection
drags do not open an editor. Boolean cells toggle and commit immediately with
User-origin value events. Interactive renderers can provide `GridColumn::clickValue`
to compute a typed value from the cell presentation and pointer position. The
callback must not mutate the model. Returning nullopt opens the ordinary editor.
Immediate actions share model validation, edit-begin and value-change vetoes,
read-only restrictions, and commit/cancel notifications with normal cell edits.
Its checkbox appears in the demo configuration.

Set a renderer column's `dragValue` to true to reuse `clickValue` for pointer
dragging. `GridOptions::dragEditing` enables or disables these gestures globally,
independently of single-click editor triggers. The demo progress column enables
this by default. Holding the mouse captures the pointer and previews the bar's
value, including when the pointer leaves the cell. Release performs one validated
User-origin commit. Escape, capture/focus loss, read-only changes, viewport resizing,
model replacement, or external updates to the cell cancel the preview. Sorting
runs after commitment, so rows remain stable while dragging. Renderer previews
use draft values while the model retains its accepted value. `Editing()` includes
these gestures, while `ActiveEditor()` remains null for renderer-only edits. Enter and
valid focus loss commit, Escape cancels, and Tab commits before navigating.
Text, integer, decimal, Boolean and choice editors reuse existing LWSUI controls.
Custom editors derive from `GridCellEditor`, own their field tree, expose typed
Draft/validation/focus operations, and publish `OnEdit` activity. The grid owns the
transaction and model write. Numeric field/rocker focus changes keep the same cell transaction active until
commit or cancellation. Custom cell renderers draw inside the cell clip. Choice popups stay within the
owning edit transaction. Escape closes the popup first, then cancels editing.
Cancelling editing or enabling read-only also closes an editor-owned popup.

Draft previews do not modify the model. Invalid parsing, bounds, model validation,
or an `OnCellValueChanging` veto retains the draft and blocks commitment/navigation.
Read-only restrictions exist at grid, column and model-cell levels. Enabling a
restriction cancels affected editing while selection, copying and navigation remain
available. Programmatic assignments validate data but bypass user editability.
External changes to an edited cell cancel its draft to prevent overwriting that update.

Header clicks cycle ascending, descending and unsorted order. Reordering stays
within a pinned region. Dragging/resizing does not trigger sorting. Escape,
focus loss, detachment and viewport resizing cancel column drags. Right-click or
Shift+F10 offers copy, editing, sorting, auto-sizing, pinning, hiding and restoring columns.

Ctrl+C copies selected cells/rows in display order, using formatted values and
optional headers. Sparse cell selections retain empty fields so columns remain
aligned, and headers include the copied columns. Fields with tabs, quotes or
newlines use quoted TSV escaping.
Clipboard paste into the active text editor uses TextBox behavior. Multi-cell paste
is deferred.

## Events and models

Cell click/double-click/enter/leave/context, current-cell, selection, edit lifecycle,
value changing/changed, validation, header, sort, column move/resize/pinning, and
scroll events use the existing move-only `Event` connections. Request events can
veto edits, value changes, sorting, moving, resizing and pinning. Selection deltas
use an empty column identity for whole rows. Column resizing reports preview,
commit and cancel phases. No-op value commits do not emit value-changed events.

Programmatic value setters are silent by default. `SetCellValue(cell, value, true)` opts
into value notifications with Programmatic origin. Model notifications always
refresh the view and do not synthesize user edit events.

External `GridModel` implementations provide a column schema, row count, stable
unique row identities, efficient IndexOf, typed values, cell editability, validated
SetValue and OnChange notifications. Publish completed Value, Structure or
Editability changes. A structural schema change must leave the model valid.
Models are retained through shared ownership. The in-memory model supports row
insertion/removal, per-cell read-only flags, and a write-validation callback.

The implementation separates row projection, selection, column geometry, value
presentation and editor adapters. Public model types depend on neither controls
nor windows. Existing numeric editors and grid formatting share numeric-text
helpers, and choices share a lightweight public type. Mouse, keyboard and menu
operations use the same validated grid methods.

## Demo and verification

Build `LWSUIGridDemo` with `LWSUI_BUILD_DEMO=ON`. On Windows it is a GUI executable
without a console window. Interactive fatal errors use a dialog, while automated
runs retain redirected diagnostics and exit codes. Its ComboBox offers Comfortable,
Compact, Striped, Styled Headers, Read-only, Mixed Editors, Sticky Columns,
Single-column List and Large External Model presets. The right-side scrollable
panel changes configuration immediately through two columns of compact checkboxes.
Every preset has a purpose description supplied by the same private preset catalog
that supplies its choices and configuration. The demo uses a 1180 x 740 client
area, 14-pixel grid text, 28-pixel rows and 30-pixel headers. Compact uses 24-pixel
rows and 26-pixel headers. Configuration controls use 12-pixel text and 14-pixel
checkbox markers, with scrolling available at the 900 x 600 minimum size.
Library sizing defaults remain unchanged, and all three themes retain these
compact demo styles. Light is the LWSUI and demo default. Explicit Dark and Warm
presets remain available. Mixed Editors enables single-click editing, with direct
checkbox toggles and progress-bar clicks and drags snapped to 5% increments. Progress cells
demonstrate custom rendering and Tag cells demonstrate a custom uppercase editor. Empty names are
rejected by model validation. Edited sample data survives presentation changes.
The external model generates one million rows synchronously without allocating
one control or stored record per row.

`--smoke` exercises all nine presets, verifies immediate checkbox actions and progress dragging across row boundaries,
and exits. `--snapshot` also writes
`grid-<preset>.ppm` for every preset in the working directory, plus Light/Warm
palette snapshots and a scrolled-header example. `--snapshot-narrow` captures the minimum-size layout using
`-narrow` filenames. `LWSTests "[grid]"`
checks models, transactions, selection, geometry, virtualization, events, focus,
keyboard/pointer input and scrollbar orientation. CTest includes GridDemoSmoke.

The canonical future TODOs are comments in [GridView.hpp](include/LWSUI/GridView.hpp).
Research references are [Dear ImGui tables](https://github.com/ocornut/imgui/blob/master/imgui.h),
[Qt QHeaderView](https://github.com/qt/qtbase/blob/dev/src/widgets/itemviews/qheaderview.h),
[Avalonia DataGrid](https://github.com/AvaloniaUI/Avalonia/blob/master/src/Avalonia.Controls.DataGrid/DataGrid.cs),
[Qt QTableView](https://github.com/qt/qtbase/blob/dev/src/widgets/itemviews/qtableview.h),
and [wxWidgets grid](https://github.com/wxWidgets/wxWidgets/blob/master/include/wx/generic/grid.h).
