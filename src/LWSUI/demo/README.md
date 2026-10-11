# LWSUI showcase

`LWSUIDemo` opens a connected workspace and two companion windows. It uses one
platform context and event loop on Windows and Wayland. Every window starts in
Light theme; the Theme menus also offer Dark and Warm. All sizes below are logical
pixels; monitor scaling changes physical pixel sizes.

## Windows and native panes

| Surface | Behavior |
| --- | --- |
| Workspace | Captionless and resizable, initially 1100 x 760; horizontal menu includes window controls |
| Controls Gallery | Resizable, initially 600 x 720, with scrollable control examples |
| Menu Gallery | Fixed 560 x 620 client area, initially with a vertical menu |
| Container Gallery | Native child pane; resizes with the workspace and divider |
| Live Preview | Native child pane; responds to controls in other windows |
| Fixed Sample | Native child pane; stays 240 x 160 while its parent resizes |

The three child panes are separate HWNDs on Windows and subsurfaces on Wayland.
Controls and nested containers within a pane share that pane's native window.
The caption and explanatory text identify each example. The fixed pane is hidden
when the compositor supplies too little space; a message explains how to reveal it.

Drag the workspace divider, or focus it and press Left/Right. Home restores the
split. Closing a companion hides it; the Workspace's Window menu reopens it with
its state intact. Closing the Workspace or choosing Exit ends the application.
Windows initial positions are offset within the work area; Wayland placement is
controlled by the compositor.

## Captionless Workspace

The Workspace's top menu bar contains Minimize, Maximize/Restore, and Close.
Drag the unused space between menu labels and window buttons to move the window.
The Workspace explicitly enables double-click maximize/restore on caption space.
Window > Live resize redraw toggles immediate redraw for the managed windows and
child panes; it is off at startup.
This calls `LWS::Window::BeginWindowDrag(Move)` directly during the pointer press;
it does not move the window by repeatedly changing coordinates. Menu labels and
window buttons are not drag handles. Native edge resizing remains available.

Alt/F10 enters menu navigation. Left/Right also reaches enabled window buttons;
Enter/Space activates them and Escape cancels. Pressing a button and releasing
outside it cancels the action. The Close button uses the same application-close
action as a native close request. Companions keep their native captions.

Wayland minimization is disabled because the existing LWS public API reports it
as unsupported. Maximize/restore and dragging use the existing APIs on both
backends; compositor configuration remains authoritative. No LWS API or backend
changes are required for these controls.

## Resizing containers without native windows

Live Preview composes five ordinary panes using four nested LWSUI `SplitPanel`
controls. The outer workspace uses the same control and places its native child
windows from the resulting pane bounds. There is no separate demo drag engine.

```text
+------------------+----------------+
| Drawing          |                |
+------------------+ Inspector      |
| Details          |                |
+------------------+----------------+
| Quick Controls   | Activity       |
+------------------+----------------+
```

Inspector spans Drawing and Details and displays the connected preview values.
Activity sits beside Quick Controls and shows recent commands and completed edits.
All five containers share the existing preview HWND/subsurface and UIHost. None
of their dividers creates, moves, or resizes a native window. The outer workspace
divider still resizes native child windows, providing a direct comparison.

The upper cross divider resizes Drawing and Details together against Inspector;
the lower cross divider independently resizes Quick Controls against Activity.
The short divider between Drawing and Details leaves Inspector unchanged. The
full-width divider above Quick Controls moves the shared boundary between the
upper and lower groups. Reset proportions are 50/30/20 for the three original
rows and 65/35 for each left/right pair.

**Rotate layout** transposes the same shared edges, placing Inspector below
Drawing and Details and Activity below Quick Controls. Controls and state are
retained. Below 496 pixels of available width, rotation falls back to the default
layout with an explanation; the requested rotation returns when space permits.
Content-based minimums constrain each divider. In smaller viewports they scale
down, and each container remains independently scrollable.

Dragging preserves keyboard focus and uncommitted edits. Tab focuses dividers;
the relevant arrow keys adjust their position by 10 logical pixels. Home or a double-click restores the focused divider;
**Reset Split** restores all four inner splits; **Reset Layout** resets both outer
and inner splits. Neither resets preview values or the selected theme. Escape,
capture/focus loss, orientation changes, and resizing during a drag cancel it and
release capture. Scroll offsets are retained where the new content extent permits.

Quick Controls shares Enabled and Opacity with Controls Gallery. Reset values
cancels drafts/previews before restoring shared values. Details retains scene,
quality, count, selected file, and the original explanatory information.

## Complete catalog

The catalog registers concrete controls and the two extension examples. Its
inventory is checked by the showcase integration tests.

| Location | Named examples |
| --- | --- |
| Controls: Text | Label, Button (normal/flat/disabled), TextBox (single-line/multiline notes/read-only/borderless/placeholder/validation) |
| Controls: Choices | CheckBox, standalone RadioButton pair, RadioGroup, ComboBox |
| Controls: Values | NumericEdit<int64_t>, NumericEdit<double>, Slider (editable/read-only values, outline/filled), standalone ScrollBar |
| Controls: Color and files | Standalone ColorSwatch, ColorPicker, FilePicker |
| Container pane | Panel, DockPanel, FlowPanel, GlyphButton, Image, horizontal/vertical StackPanel, fixed/proportional Grid, ScrollView (reserved/overlay bars), TreeView, Branch, Row |
| Workspace and Preview | Nested SplitPanel controls and custom Container with native child placement; custom Control drawing through Canvas |
| All desktop windows | MenuBar; Menu Gallery adds cascades, separators, checked/disabled entries, orientation, docking, floating and detachment |

The section buttons at the top of Controls Gallery jump to the corresponding
examples. Each example explains its behavior beside the control. Use the text
editor's context menu or right-click the enable checkbox for context-menu actions.

Values update the shared preview, and commands appear in Menu Gallery's history.
Escape cancels active edits, and ColorPicker's Cancel restores the previous color.
Reset values restores the connected preview defaults. FilePicker displays the
selected path and does not modify the file. State is not persisted.

## Menus

Main and Controls menus are horizontal. In Menu Gallery, use **View > Menu bar**
to switch docking, vertical/horizontal orientation, floating, or detachment. Drag
floating and detached bars by their dotted grip. Alt/F10, arrows, Enter, and Escape
provide keyboard navigation; Shift+F10 opens a focused control's context menu.
The disabled Undo/Redo entries demonstrate shortcut hints, not an undo history.

Native dropdowns are hosted by desktop windows on both backends. Wayland does not
support popup ownership by subsurface children; embedded panes forward application
menu activation to the Workspace. Their ordinary container/control examples do
not require native dropdown ownership.

## Verification and snapshots

Build target `LWSUIDemo` with `LWSUI_BUILD_DEMO=ON`. When tests are also enabled,
`LWSTests "[showcase]"` checks catalog coverage, shared values, pane parenting,
resizing, splitter cancellation, and companion reopening.

- `LWSUIDemo --smoke`: open all windows, exit after two seconds, and verify the catalog and native panes.
- `LWSUIDemo --snapshot`: also render images into the working directory.
- `LWSUIDemo --snapshot-narrow`: capture an 800 x 580 workspace and a 420 x 600 Controls Gallery.

Images are `workspace.ppm` (including native child contents), `controls.ppm`,
`menu-gallery.ppm`, `container-pane.ppm`, `preview-pane.ppm`, `preview-stacked.ppm`, `preview-side-by-side.ppm`, `fixed-pane.ppm`, and
`catalog-N.ppm` and `containers-N.ppm` pages covering every section of both scrolling galleries. At narrow widths, the alternate-layout image records the responsive default-layout fallback. Snapshots use the
normal control rendering paths; they are not desktop captures of native captions.

The application entry point is `../demo.cpp`. `MenuGallery.cpp` retains the menu
orientation/detachment demo, while `Showcase.cpp` owns the connected galleries,
window lifetimes, layout, catalog, and snapshots. `SplitPreview.cpp` implements the private container-only splitter; it has no native window ownership. These helpers are private to the
demo and are not additional LWSUI public APIs.

## Standalone grid demo

`LWSUIGridDemo` is a separate GUI executable without a Windows console window with nine layout presets and a scrollable
two-column feature panel to the right of the grid, including single-click editing
and sticky-header options. In single-click mode, checkboxes toggle and progress
bars support captured dragging with a live preview and a commit on release.
The Drag value edits checkbox controls these gestures independently of single-click
editing. Escape cancels a drag. Every preset explains its purpose. The compact demo
styles leave reusable control defaults unchanged. See [GridView](../GridView.md) for the full
configuration and event contract. It shares the ordinary LWSUI library and is
built under `LWSUI_BUILD_DEMO`. Its `--smoke` and `--snapshot` modes cover all presets.
`--snapshot-narrow` also verifies the minimum-size two-column configuration panel.

The Text section also demonstrates [TextBox filtering and fixed masks](../TextInput.md) for numbers, hexadecimal input, dates, times, phone numbers, and product IDs. Invalid edits preserve the current value and show rejection feedback. Date and time examples apply semantic validation separately from mask syntax.

## Distinguishing examples

Controls Gallery groups examples into bordered cards with a name, description, control, and related feedback. Ordinary input fields have visible frames. The Borderless example deliberately omits its field frame. The Input policies category contains eight filtering and mask examples. Navigation buttons wrap at narrow widths, and card borders follow the selected theme.

## Simple example and events

LWSUISimpleExample starts in Light theme at 420 x 640 logical pixels, with a 320 x 480 minimum. On Windows it uses the GUI subsystem and opens without a console window. The upper area contains two multiline editors, the name field, greeting, and Clear button. A draggable divider separates the bottom Events pane.

The Events pane displays the latest 200 entries in chronological order. It records textbox preview, commit, and cancel events, divider edits, Clear clicks, and window show, resize, and close events. New entries reveal themselves without moving keyboard focus. The read-only viewer supports selection, copy, and scrolling. Clear log empties the event history without resetting the example controls.

Fatal Windows errors appear in a dialog and return a nonzero exit status. The --smoke option checks events, editing, clearing, resizing, and the default theme, then exits. Smoke failures use exit status without a dialog.

Gallery snapshots also include controls-light.ppm, controls-dark.ppm, controls-warm.ppm and catalog-THEME-N.ppm pages for each theme.

## Showcase output and network launch

LWSUIDemo uses the Windows GUI subsystem and does not create a console. Its dedicated Events window displays the latest 200 output entries in chronological order, with selection, copy, scrolling, and Clear log. Closing the Events window hides it. Window > Events reopens it with its retained history. The existing Activity and Menu Gallery summaries show the latest 18 entries from the same history. Fatal startup errors use a Windows dialog, while smoke runs return a failure status without showing a dialog.

The GridDemo smoke check exercises native Win32 mouse messages, including checkbox clicks and progress dragging. It can be launched from a local path or a UNC path to check network execution. The UI demos use normal Windows image-loading behavior without a special network-loader flag.
