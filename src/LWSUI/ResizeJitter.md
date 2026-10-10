# Live resize jitter investigation

Updated: 2026-10-09. The general fixes and automated regression tests are in place.
The user confirmed that live resizing now works well in LWSUIDemo.

## Change record

This record covers resize jitter in LWSUIDemo, independently of the ongoing
GridView work. The existing Git diff records the source changes. No unrelated
working-tree changes have been staged or committed.

| Change | Status | Reason and evidence |
| --- | --- | --- |
| Text-origin snapping in [CanvasWin32.cpp](source/CanvasWin32.cpp) | Retained | Snap the aligned text run before drawing text and highlights, preserving glyph advances. The user reported improved vertical text placement. |
| Control-rectangle edge snapping | Removed | It did not resolve the reported jitter. General UI checks also showed outward rounding expanding an allotted 90-unit width to 90.4 units at fractional DPI. Logical bounds now retain the parent's allocation. |
| Stable Windows drawing scale in [UIHost.cpp](source/UIHost.cpp) | Active | Use native window DPI instead of physical dimensions divided by rounded logical dimensions. Account for remaining framebuffer pixels through a fractional layout viewport rather than stretching controls and glyphs. |
| Shared width reconciliation in [Control.cpp](source/Control.cpp) | Active | Remeasure at the actual allocated width before arranging. This covers grids, flows and wrapped labels. Allocation hints match the requested width and measurement kind, and expire on explicit layout changes or the next host update. Internal reconciliation retains them. Intrinsic and minimum-width probes must not erase the correction for the final allocation. |
| Settle layout before presentation | Active | Host updates perform bounded reconciliation passes and reject frames while layout is dirty. This prevents displaying a new row plan with an old parent height. |

The shared correction replaces the earlier FlowPanel-only attempt. FlowPanel
uses the row plan from corrected measurement, without another independent wrap
decision. Comments explain the retained text snapping, allocation hints, update
scope, stable scale and frame-presentation guard.

## Reproduction and expected behavior

Enable Window > Live resize redraw in LWSUIDemo. In the Workspace's Container
Gallery, resize around the glyph button and `FlowPanel wraps intrinsic controls`
label. The label is single-line, and the FlowPanel wraps controls as items.
One height change when the two items no longer fit together is expected.
Repeated row or height oscillation at a stable size is not.

The general path matters beyond this example. Fixed controls should retain glyph
shape and spacing at fixed DPI. Returning from size A through B to A should
restore the original geometry and pixels. Centered controls may translate by a
whole pixel as their container changes size.

## Automated regression coverage

[Tests](../../tests/test_resize_jitter.cpp) cover twelve Windows cases:

- Actual allocation differing from measurement, with stable repeated layout.
- One row-wrap transition in each direction during a width sweep.
- The same correction in a general Grid container.
- Alternating intrinsic and minimum-width probes without losing the allocation hint.
- Explicit invalidation resetting width corrections in detached controls.
- Menu orientation and docking changing during arrangement without reusing stale widths,
  with live resize rendering enabled and disabled.
- Backend window DPI remaining the drawing scale across client-size changes, using
  the existing legacy-compatible DPI cache rather than importing newer Windows APIs.
- Identical aligned crops of a fixed control, including text and borders, across
  consecutive framebuffer width and height changes at 100%, 125% and 150% scale.
- Identical aligned text ink for subpixel translations, including centered text.
- Identical complete raster and geometry after A-to-B-to-A resizing at all three scales.
- The real showcase glyph-and-label pair crossing its wrap boundary once per
  direction and restoring each recorded rectangle on the reverse sweep.
- A negative control that deliberately restores the old rounded-client scale
  calculation and proves that the raster comparison detects glyph distortion.

The raster tests render through the normal Windows Canvas into memory, copying
pixels before the next frame. They use a fresh baseline per run, without stored
font-dependent golden images or changes to desktop display settings. The three
scales are offscreen rendering scales. The native-window test runs at the current
monitor DPI. Aligned glyph crops allow intended whole-pixel translation while
checking internal shape and spacing. Focus, hover and caret animation are excluded
from these static fixtures.

The five generic geometry cases also run on Wayland. Broader UI checks include
menus, splitter input, caption actions, clipping, size limits and showcase layout.

## Future TODOs

- Add a shared `Window::GetContentScale()` API exposing the backend's cached nominal
  scale on Windows and Wayland, distinct from `ClientAreaMetrics::Scale()`'s rounded
  dimension ratio. Validate Wayland fractional-scale resize raster stability and
  preserve its logical viewport, compositor mapping and input alignment before
  switching UIHost to this API.
- Once the shared API replaces its callers and tests, remove the temporary
  `LWS::Win32::GetDpi()` extension and its backend accessor. Keep the existing DPI
  cache and legacy Windows fallback logic.

## Verification

Run `LWSTests "[resize-jitter]"` for the focused suite and
`LWSTests "[ui]~[grid]"` for the independent general UI checks. Grid tests belong
to the main thread's separate work.

The isolated review fixes passed 1,319 assertions across twelve Windows jitter
cases and 2,999 assertions across 55 Windows UI cases. Wayland passed 1,574
assertions across 44 UI cases. LWSUIDemo passed its smoke check on both platforms.
The Windows demo no longer imports GetDpiForWindow directly. Older Windows
compatibility was checked through its import table, without an older OS runtime.

The user confirmed the live-resize result. Continued acceptance requires fixed monitor DPI, with
expected responsive wrapping and movement, consistent hit testing, and no
repeated size oscillation or glyph-spacing shimmer at the same layout state.
