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

The private implementation separates state and transactions in `GridView.cpp`,
layout and drawing in `GridViewRender.cpp`, and input routing in `GridViewInput.cpp`.
`GridViewImpl.hpp` contains shared state and declarations without introducing a
public interface. Text alignment and Label ellipsis use shared internal helpers,
and header measurement and rendering resolve the same font.

Numeric adapters use `NumericEdit::Draft()`, `SetValidation()` and `FocusTarget()`
instead of relying on child order. Draft inspection parses and checks bounds
without accepting the value or raising events. Numeric editors, models and sliders
share numeric validation while retaining the slider's stricter range rules.

## Demo and verification

Build `LWSUIGridDemo` with `LWSUI_BUILD_DEMO=ON`. On Windows it is a GUI executable
without a console window. Interactive fatal errors use a dialog, while automated
runs retain redirected diagnostics and exit codes. Its ComboBox offers Comfortable,
Compact, Striped, Styled Headers, Read-only, Mixed Editors, Sticky Columns,
Single-column List, Large External Model, Random 100 million rows and Wide columns presets. The right-side scrollable
panel changes configuration immediately through three columns of compact checkboxes.
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

`--smoke` exercises all eleven presets, verifies immediate checkbox actions and progress dragging across row boundaries,
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

## Dynamic-resolution scrolling

`GridOptions::dynamicResolution` is opt-in and defaults to false. During a scrollbar
drag, move the pointer inward into the grid to reduce sensitivity
exponentially. Moving sideways alone changes speed without changing the scroll
position. Track clicks retain their initial jump and engage dynamic resolution
for subsequent dragging. Returning toward the scrollbar restores normal sensitivity without a
jump. The same behavior applies to horizontal scrollbars when moving upward.

`centerResolution` selects `ScrollResolution::Row` or `ScrollResolution::Pixel`.
At the configured inward distance, each logical pointer pixel scrolls at most one row-height or one content
pixel respectively, multiplied by `resolutionFraction`. Beyond that distance, the fine sensitivity stays constant.
Horizontal row mode uses row height as an estimated horizontal scroll unit.
TODO: Find a better horizontal unit based on column widths or another meaningful
measure. The resolution fraction allows fine control of small ranges using fractional row or pixel movement.

Standalone `ScrollBar` users enable `SetDynamicResolution(true)` and supply the
containing bounds, maximum content offset, line size, and mode through
`ConfigureResolution`. `ScrollSpeed` and `OnScrollSpeed` expose display feedback.
Release, cancellation, capture loss, detach, and configuration changes reset it.

`resolutionGuide` optionally renders a noninteractive drag guide, endpoint reference,
pointer marker, and one current-speed label. It defaults to false and is enabled in the demo.
The demo toolbar displays a read-only progress bar labeled **Scroll speed**.
Normal speed is 100%. At a full-unit fraction, row precision is 1% and pixel
precision is `1 / rowHeight` percent. Fine feedback is multiplied by the configured fraction. These are a display scale rather than a physical ratio
of fine sensitivity to normal sensitivity. The indicator resets after dragging.

`SetPreciseScrollOffset`, `PreciseScrollOffset`, and `OnPreciseScroll` use
`GridScrollOffset` with double coordinates. Existing float scroll methods and
`OnScroll` remain available, but lose precision for very large offsets.

The **Random 100 million rows** preset generates repeatable pseudorandom values
on demand without allocating the dataset. It opts into dynamic resolution and
disables editing, sorting, multi-row selection, select-all, and resizing/auto-size
operations to keep interaction bounded. The original million-row preset remains.

## Line numbers

Set `GridOptions::lineNumbers = true` to show a read-only **Line** column pinned
at the left edge. The option defaults to false and is enabled in the Random
100 million rows preset. Numbers are one-based positions in the current display
order, so sorting renumbers positions rather than displaying model row IDs.
The column sizes itself for the row count and stays visible during horizontal
scrolling. It respects row height, striping, header visibility, and sticky headers.
It is a presentation column, separate from model data and `Columns()`. It cannot
be edited, sorted, resized, reordered, hidden through column menus, or copied.
Rendering generates only visible numbers and does not read model cell values.

## Configurable headers and inward distance

`lineNumberLabel` sets the read-only line-number column header. It defaults to
`"Line"` and can be empty. Column measurement accounts for the configured label.
The demo enables its label field when the Line numbers checkbox is checked.

`inwardDistance` is the fraction of the containing control's full perpendicular
size traversed inward from the scrollbar's inner edge. It defaults to `0.5`,
approximately the center. A vertical scrollbar uses control width, a horizontal
scrollbar uses control height. Valid values are finite, greater than zero, and
at most one. At 100% the pointer can reach past the opposite edge by the scrollbar
gutter width because measurement starts at the inner edge. Capture keeps the
active drag working outside the control. Changing distance cancels the gesture.
Standalone ScrollBar users configure it through `SetInwardDistance`.

The demo provides Dynamic resolution and Show guide checkboxes, a 1%-100%
distance field, and Row/Pixel precision selection. Distance and precision are
available when dynamic resolution is enabled. Guide rendering is shared with
ScrollView through `ScrollBar::RenderResolutionGuide`.

The Wide columns preset uses 1,000 virtual rows and 1,000 fixed-width columns
with stable generated values. It enables dynamic resolution and line numbers.
Editing, sorting, select-all, multi-row selection and auto-sizing are disabled.
Its horizontal scrollbar demonstrates a substantial sensitivity reduction.

## Outward scrollbar acceleration

Outward acceleration is a separate opt-in and requires dynamic resolution.
Set `outwardAcceleration` in GridOptions or ScrollViewOptions, or call
`ScrollBar::SetOutwardAcceleration(true)` for a standalone scrollbar.
`maximumScrollSpeed` defaults to 400 percent and must be finite and at least
100. `outwardDistance` defaults to 0.1 and must be a finite fraction in (0, 1].
Standalone scrollbars expose SetMaximumScrollSpeed and SetOutwardDistance.

Distance starts at the scrollbar's outer edge and uses containing-control width
for vertical bars and height for horizontal bars. Speed rises exponentially to
the maximum, then remains capped. The scrollbar thickness stays at normal speed.
Moving perpendicular to the scrolling axis alone changes speed without moving
content. Returning inward preserves the existing fine-scroll behavior.
Physical screen edges can limit outward pointer travel even with capture.

Both demos provide an Outward acceleration checkbox, maximum-speed field
(100 to 10,000 percent) and outward-distance field (1 to 100 percent).
The speed progress bar uses the configured maximum with a marker at normal
100 percent when acceleration is enabled. The single speed readout and guide
remain inside the control while the pointer travels outside it.

## Resolution fraction

`resolutionFraction` sets the fraction of the selected row or pixel unit scrolled
per logical pointer pixel at the inward endpoint. It defaults to 0.2 and accepts
positive finite values without an upper bound. Row mode targets row height multiplied by the fraction.
Pixel mode targets one content pixel multiplied by the fraction. For a 28-pixel
row, the default targets 5.6 content pixels in row mode and 0.2 in pixel mode.
If normal sensitivity is already lower, inward dragging retains normal speed.
Changing the fraction cancels an active drag.

Standalone scrollbars expose `SetResolutionFraction` and `ResolutionFraction`.
Both demos provide a Fine unit fraction % field enabled with dynamic resolution.
The field defaults to 20%, with a minimum of 0.01% and no upper bound.
For example, 200% in pixel mode targets two content pixels per logical pointer pixel.
Normal feedback remains 100%. Fine feedback uses the existing unit scale
multiplied by the fraction, giving default row feedback of 0.2% and pixel
feedback of 0.2/lineSize%. These display units are not a percentage of normal
physical sensitivity.
