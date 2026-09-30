# LWS

LWS is a compact C++26 windowing layer with explicit platform ownership. It provides portable window lifecycle,
input, events, immutable cursors and icons, timers, clipboard, drag-and-drop, and bitmap presentation while keeping
native backends private.

## Supported platforms

| Platform | Backend | Status |
| --- | --- | --- |
| Windows | Win32 | Supported |
| Linux | Wayland | Supported when the Wayland development packages and protocols are available |
| Linux | X11 | Source scaffold only; it is not reported as an available backend |

`PlatformContext::GetAvailableBackends()` reports compiled complete backends. Context-scoped `Supports()` queries
optional behavior without consulting global state.

## Build

```sh
cmake -S . -B build -DLWS_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Embed LWS with `add_subdirectory` and link `LWSLib`.

## Quick start

```cpp
#include <LWS/Platform.hpp>
#include <LWS/Window.hpp>
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/Platform.hpp>
#endif

int main()
{
#ifdef LWS_HAS_WIN32_BACKEND
    if (LWS::Win32::BootstrapProcess() != LWS::Result::Success)
        return 1;
#endif

    LWS::PlatformContext platform;
    const LWS::PlatformConfig platformConfig{
#ifdef LWS_HAS_WIN32_BACKEND
        .backend = LWS::BackendId::Win32,
#else
        .backend = LWS::BackendId::Wayland,
#endif
    };
    if (platform.Init(platformConfig) != LWS::Result::Success)
        return 1;

    {
        LWS::Window window(platform);
        auto connection = window.Listen(
            [&](const LWS::AnyEvent& event)
            {
                if (std::holds_alternative<LWS::EventWindowDestroyed>(event))
                    platform.RequestQuit();
                return LWS::EventResponse::Unhandled;
            });
        const LWS::WindowConfig config{
            .clientSize = {800, 600},
            .styles = LWS::WindowStyleFlags(LWS::WindowStyle::Caption | LWS::WindowStyle::CloseButton),
            .visible = true,
        };
        if (!connection.has_value() || window.Create(config) != LWS::Result::Success)
            return 1;

        platform.RunMessageLoop();
        std::ignore = window.Destroy();
    }
    return platform.Shutdown() == LWS::Result::Success && !platform.GetFailure().has_value() ? 0 : 1;
}
```

## Ownership and threading

- One `PlatformContext` owns one backend, one UI thread, one task queue, and one message loop.
- Failed context initialization may retry. Successful shutdown is terminal.
- A `Window` permanently borrows an active context and has at most one successful native lifetime.
- Destroy every bound window and persistent service before shutting its context down.
- Context-bound operations require the context thread. `PostTask()`, `RequestQuit()`, and `IsCurrentThread()` are the
  cross-thread-safe instance operations.
- User callbacks execute synchronously on the context thread. Exceptions are reported through the installed
  non-throwing context handler and never unwind through native callbacks.

## Bitmap presentation

`Window::PresentBitmap()` borrows the caller's pixels only for the duration of the call. The caller may reuse or
release that storage on return. The bitmap must match the current framebuffer dimensions; Wayland accepts tightly
packed, top-down, premultiplied BGRA pixels.

Wayland copies each accepted frame directly into reusable shared memory. At most three presentation buffers are
submitted to the compositor at once, with one additional unsubmitted buffer holding the newest pending frame.
Repeated pending frames overwrite that idle buffer. When a submitted buffer is released, LWS submits the pending
buffer itself, without another pixel copy. Released buffers are reused, and obsolete idle sizes are discarded.

The copying API is an intentional simplicity tradeoff. An optional acquire/write/present API could eliminate the
remaining copy for clients that render directly into LWS shared memory. It was not selected because it would expose
exclusive buffer access and compositor release coordination to clients, including buffer exhaustion, resize,
cancellation, window destruction, and background-render completion. LWS keeps that coordination internal so clients
can present ordinary pixel storage without managing presentation-buffer leases. Such an additive API remains a
future option if a measured client workload justifies the additional contract.

## Portable and typed APIs

Shared code uses `Window`, `AnyEvent`, `ClientAreaMetrics`, and logical client coordinates. `WindowConfig::clientSize`
and `WindowPlacementRequest::clientSize` describe the drawable client area with `LogicalSize`; native title bars,
borders, shadows, and other outer decorations are outside that size contract. `ClientAreaMetrics::pixels` is an
optional, non-convertible `PixelSize` and is authoritative for native rendering when present.

Logical size is the stable layout size an application aims to preserve when display scaling changes. On Win32,
logical units are normalized to 96 DPI; on Wayland, they are surface coordinates. A width of 600 logical units uses
600 pixels at 100% scaling, 900 at 150%, and 1200 at 200%. Logical units do not measure inches or millimetres, and
resizing, native window constraints, or integer rounding can still change the reported logical dimensions.

`GetClientAreaMetrics()` returns a `ClientAreaMetrics` value directly. When native metrics are available, it pairs
logical and pixel dimensions from the last published backend update. Size listeners observe that pair before
running; show-state listeners can run earlier and observe the previous pair. Nested dispatch can publish a newer
pair before an outer listener resumes. Otherwise, `logical` comes from the backend while created or
the stored configuration outside that state, and `pixels` is empty. Logical size remains useful for startup layout.
Pixels are unavailable before initial configuration, during Wayland remapping, and after native teardown or backend
failure. They can remain available during orderly cleanup even though `IsConfigured()` is false. An available
zero-sized client area is distinct from missing native metrics. On Wayland, later metrics updates can also result
from LWS applying size requests without a new compositor acknowledgment. Before initial configuration or while
remapping, size and scale requests update backend layout state without publishing paired metrics.

Use one snapshot for rendering dimensions and coordinate conversion so both use the same scale:

```cpp
const auto metrics = window.GetClientAreaMetrics();
if (const auto scale = metrics.Scale(); scale && metrics.pixels->x > 0 && metrics.pixels->y > 0)
{
    ResizeRenderTarget(*metrics.pixels);
    const auto logicalPosition = window.GetMousePosition();
    const double pixelX = logicalPosition.x * scale->x;
    const double pixelY = logicalPosition.y * scale->y;
    HandlePointerInPixels(pixelX, pixelY);
}
```

`ResizeRenderTarget()` and `HandlePointerInPixels()` above represent application code. `Scale()` computes
`pixels / logical` on each axis, returning `nullopt` if pixels are absent or either logical dimension is zero.
Rounding can make these effective ratios
differ between axes and from nominal display scale; use the reported pixel size for render targets. On Win32,
pixel dimensions come from `GetClientRect()` or native resize notifications.

Wayland scale is compositor-provided per-surface state, not physical-monitor DPI. With `wp_fractional_scale_v1` and
`wp_viewporter`, LWS allocates each pixel dimension with `ceil(logical * preferredScale)`, uses buffer scale one, and
sets the viewport destination to the logical size. Without those protocols, LWS uses the maximum integer scale of the
outputs containing the surface. A scale change that alters the paired pixel size publishes
`EventClientAreaSizeChanged` even when logical size is unchanged; clients must use that pixel size rather than monitor
scale or reconstructed dimensions.

`RequestPlacement()` accepts optional position and logical client size: omitted fields stay unchanged, and an empty
request or nonpositive supplied size is invalid. Combined Win32 movement/resizing uses one native geometry operation;
redraw and callbacks retain their own timing. Placement does not issue a show-state request or override one made
by a geometry listener. Wayland top-level positions cannot be set or queried, so an explicit
position returns `NotSupported` without applying an accompanying resize. Wayland child position and content can take
effect on separate parent/child commits. `GetPlacement()` returns optional position and logical client size; it does
not promise an atomic native observation. Win32 top-level position follows the restored placement in DPI-normalized
screen coordinates, matching position requests, while child positions are relative to their parent's client area.

`GetClientSizeLimits()` and `SetClientSizeLimits()` exchange a `ClientSizeLimits` with `minimum` and `maximum` logical
dimensions. Zero means no application-specified limit on that axis. Values must be nonnegative, and a nonzero maximum
must be at least its minimum. Native constraints may still apply. Creation uses the same type through
`WindowConfig::clientSizeLimits`.

`RequestShowState(WindowShowState::Maximized)` requests a windowed, maximized top-level window even from fullscreen.
Win32 retains the current monitor and saved normal client size where it fits. Wayland sends both leaving-fullscreen
and maximization requests, even when earlier configuration events are in flight. Success means the request was
issued; the compositor controls the resulting state and timing. Child maximization returns `NotSupported`.

`SetMouseCursor(Cursor::FromShape(CursorShape::Arrow))` resets the cursor while preserving its visibility.
`SetWindowIcon(std::nullopt)` clears the custom icon. Both preserve the previous reset operations' lifecycle and
platform support; a moved-from cursor or engaged moved-from icon remains invalid, and repeated immutable resources
retain native handle reuse.

Platform translation units may opt into typed extensions:

```cpp
#ifdef LWS_HAS_WIN32_BACKEND
#include <LWS/Win32/WindowExtensions.hpp>

auto connection = LWS::Win32::Listen(
    window,
    [](const LWS::Win32::PlatformEvent& event) -> std::optional<LRESULT>
    {
        if (const auto* paint = std::get_if<LWS::Win32::PaintEvent>(&event))
        {
            DrawSidebar(paint->deviceContext, paint->invalidRect);
            return 0;
        }
        return std::nullopt;
    });
#endif
```

Typed native handles are borrowed from successful creation until native teardown begins, including orderly
`EventWindowDestroying` notification. Release dependent swap chains, EGL surfaces, `wl_egl_window` objects, and native
registrations in that notification. Backend failure invalidates public handle access immediately.

## Lifecycle and migration

- File-dialog filters are a `ListFileDialogFilters` collection of `FileDialogFilter` values. Store/pass that collection
  directly instead of constructing a `FileDialogFilterBuilder` and calling `GetFilters()`.
- `Win32::GetHwnd()` and `Wayland::GetSurface()` take `const Window&`; mutable-window calls continue to work.
- Portable `AnyEvent` no longer includes the unused `EventRawPlatform`; use the existing typed platform listeners.
- `PlatformContext::AssertCurrentThread()` is private; clients can query `IsCurrentThread()`.

- Read backend identity through `window.GetPlatformContext().GetBackendId()`; `Window::GetBackendId()` was removed.
  The context getter returns an optional because contexts can exist before initialization; a window is permanently
  bound to an initialized context.
- Replace `Window::GetClientSize()`/`GetLogicalClientSize()` with `GetClientAreaMetrics().logical`, and
  `GetClientAreaSize()` with `GetClientAreaMetrics()`. Rename `ClientAreaSize` to `ClientAreaMetrics`; check its
  optional `pixels` and `Scale()` instead of checking an outer `expected` result.
- Replace `SetPosition()`, `RequestClientSize()`, and `SetPlacement()` with `RequestPlacement()` supplying only the
  desired fields. Use `GetPlacement().position` instead of `GetPosition()`.
- Replace `GetMinClientSize()`, `GetMaxClientSize()`, and `SetMinMaxClientSize()` with `GetClientSizeLimits()` and
  `SetClientSizeLimits()`. Replace `WindowConfig::minClientSize`/`maxClientSize` with
  `clientSizeLimits.minimum`/`clientSizeLimits.maximum`.
- Replace `ResetMouseCursor()` with `SetMouseCursor(Cursor::FromShape(CursorShape::Arrow))`, and
  `ResetWindowIcon()` with `SetWindowIcon(std::nullopt)`.
- Replace `RequestMaximize()` with `RequestShowState(WindowShowState::Maximized)`. The latter now also leaves
  fullscreen and rejects child windows; restore and minimize behavior is unchanged.
- Boolean queries are `IsVisible()`, `IsTransparent()`, `IsAlwaysOnTop()`, and `IsBackgroundErasureEnabled()`;
  drag-and-drop configuration is `SetDragAndDropEnabled(bool)`. These replace the corresponding `Get...()` methods
  and `EnableDragAndDrop(bool)`. These API migrations have no compatibility aliases; rebuild clients together.
- `RunMessageLoop()` and `ProcessMessages()` return `LoopResult`. A host loop must compare explicitly with `Continue`,
  `Quit`, or `Failed`; the old Boolean use of `ProcessMessages()` requires a source update. `RunMessageLoop()` never
  returns `Continue`. Quit is sticky, including across repeated loop calls, and failure takes precedence.
- `GetFailure()` returns the first backend diagnostic, owned by the context and retained through `Shutdown()`. Failure
  rejects new posts and suppresses ordinary callbacks, including remaining listeners after nested pumping. Unstarted
  tasks are discarded; their captures are released after active dispatch unwinds or at later explicit shutdown.
- Failure does not destroy clients' C++ objects. `IsCreated()`/`IsConfigured()` and handle access become unavailable;
  clients destroy their object graph normally. Cleanup receives `EventWindowDestroying{false}` when the backend is
  already lost. No application exit, restart, or backend migration is performed by LWS.
- `EventWindowDestroying` is appended to `AnyEvent`. Update exhaustive visitors and rebuild clients and libraries
  together; this change does not claim binary compatibility with earlier headers. Embedded LLUtils must include the
  accompanying `Event::RaiseWhile()` addition used to stop notification listeners after failure.
- On orderly destruction, the parent is notified before its children, then its native window is destroyed and the
  final destroyed event is delivered. Pre-destruction cannot be consumed or cancelled. Unexpected cleanup exceptions
  are reported and later eligible cleanup listeners continue if the handler returns; without a handler LWS terminates.
- Recursive destruction of a destroying window returns `InvalidState`. Destroying descendants during an ancestor's
  pre-destruction notification, or an ancestor while a descendant is destroying, is rejected. Supported sibling removal
  remains valid once ancestor pre-destruction notification has completed.
- Native `Destroy()` may run from callbacks, but the executing C++ Window must survive all active native/event dispatch.
  Deleting it, or the active context, is a debug-diagnosed precondition violation. Deferring deletion by posting a task
  alone is insufficient if nested pumping can execute the task early. Existing supported timer self-deletion remains.
- Normal `Shutdown()` rejects active dispatch or bound objects. Otherwise it closes task acceptance and drains accepted
  tasks. New posts during the drain are rejected. Resources left bound by those tasks keep a healthy context active and
  reopen acceptance for cleanup/retry. Successful shutdown is terminal. Failure during the drain discards the rest.
- Active scheduling remains unchanged. Do expensive work on background threads and post brief, nonblocking result
  application callbacks. The queue is unbounded; callers own workload discipline.

### Window property phases

All operations retain their documented context-thread precondition. A failed context overrides native access in every
phase. Typed native handles must not be used to destroy LWS-owned windows directly.

On Wayland, style flags describe requests and local-frame policy. With server-side decorations, the compositor controls
caption buttons; `GetWindowStyles()` does not claim every requested decoration was enforced by the compositor.

| Property/operation | Pre-create | Created | Orderly cleanup notification | Destroyed or backend failed |
| --- | --- | --- | --- | --- |
| Context/backend identity | Available | Available | Available | Available until C++ destruction |
| Title, optional position, logical size, placement, min/max, styles, show state, visibility, transparency, always-on-top, erase flag | Stored defaults | Current backend values | Last stored request/observed value | Last stored request/observed value |
| Window mode | Backend's retained logical mode | Current mode | Retained mode | Retained mode |
| Client-area metrics | Logical defaults; pixels absent | Logical size always available; paired pixels after configuration | Last configured pair before native teardown | Stored logical size; pixels absent |
| Parent | None until successful creation | Bound parent | Relationship remains during notification | Cleared after final destroyed dispatch; retained until cleanup on failure |
| Focus/pointer-in-client | False | Current observation | False | False |
| Mouse position | Zero | Current observation | Zero | Zero |
| Typed handles | InvalidState | Borrowed | Borrowed until native teardown starts | InvalidState |
| Cursor/icon selection, visibility and typed app-ID/menu configuration | Allowed where supported | Allowed where supported | InvalidState | InvalidState |
| Other window mutations, presentation, drag/resize/lock requests | InvalidState | Validated operation | InvalidState | InvalidState |
| Listen | Allowed | Allowed | InvalidState | InvalidState |
| Disconnect existing listener | Allowed | Allowed | Allowed | Allowed |

These are retained logical values, not a promise to query a disconnected native system. Construction events stay
suppressed and failed Create() remains retryable; successful native lifetime is one-shot. Unsupported requests return
`NotSupported` without changing retained/native state. Malformed arguments use `InvalidArgument`, and lifecycle errors
use `InvalidState`. Service APIs with legacy non-Result signatures preserve their failure conventions: timers cannot
rearm after failure, clipboard operations return empty/error results, and notification creation reports invalid state.

## Listener publication and resource reuse

Ordinary portable and typed dispatch read stable vectors without allocating a snapshot per event. Additions made during
any nested listener traversal on a window accumulate in a pending list; it is published when the outermost traversal
completes. Publication precedes retired-capture destruction, so events reentered from those destructors see the published
list; additions made during that cleanup are deferred until the cleanup finishes.
This intentionally changes nested ordinary-event visibility: a newly registered listener does not receive events in the
current dispatch chain. Its connection is immediately queryable/disconnectable, and disconnection immediately suppresses
later invocation. Destruction notifications include pending listeners, preserving cleanup for resources and timers attached
just before a callback destroys its window. Window rejects further registration during that cleanup phase.

Retired callbacks are released outside vector mutation, and Close cannot revive pending registrations. Listener operations
remain context-thread-affine; this adds no listener locks or atomic publication. The tradeoff is extra per-window pending
state and more work on registration changes in exchange for removing steady-state vector allocation/copying.

Reapplying the same immutable custom cursor or icon to a created Win32 window reuses that window's native allocation.
Cursor visibility and WM_SETICON application still run. Creation/retry and replacement realize resources normally; each
window retains independent native resources. Custom cursor bitmaps/hotspots are pixel data and do not depend on window DPI.
Wayland's focus/serial/theme/scale-driven cursor application is unchanged.

When `wayland-server` development files are available, LWS tests build a private protocol server for deterministic seat
capability/removal/promotion, cursor focus/serial, and integer/fractional-scale checks. This dependency is test-only. Win32
DPI scenarios run in separate child processes because process DPI initialization is irreversible.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/LWS/include/LWS` | Portable public headers and typed extension headers |
| `src/LWS/source` | Portable implementation and private native backends |
| `tests` | Unit and platform integration tests |

## LWSUI and settings integration

The optional `LWSUI::LWSUI` target provides controls, layout, text rendering, popups,
focus/capture and bitmap presentation without a JSON dependency. `LWS_BUILD_UI` defaults
on for Windows/Wayland and off for the unavailable X11 scaffold. `LWSUI_BUILD_DEMO`
controls the [multi-window control showcase](src/LWSUI/demo/README.md), with fixed and resizable native child panes and a complete control catalog. Dependency builds compile LWSUI only when linked
(or explicitly requested); standalone builds include it. LWSSettings is a separate consumer.

UIHost coalesces deferred work, layout and painting through PlatformContext::PostTask.
Numeric holds arm their own timers; idle hosts have no periodic tick or tree traversal.
Wayland timers use timerfd in the context wait loop, and clipboard transfers wait on
pipe readiness and their actual timeout. Portal dialogs still use a nested timed D-Bus
pump; converting that path remains a separate asynchronous-dialog design.

UI hosts borrow a context-bound Window; editors borrow the caller's PlatformContext.
Use upstream Listen/EventConnection and ClientAreaMetrics, with logical input/layout
and exact pixel buffers. Rendering pauses while pixels are unavailable or zero-sized.
Canvas accepts per-axis ContentScale so fractional-scale rounding stays aligned.

AnyEvent additionally carries EventTextInput (committed UTF-8), EventMouseLeave and
EventMouseCaptureLost. Explicit capture is a Win32 extension:
`LWS::Win32::SetMouseCapture(window, capture)` in `LWS/Win32/WindowExtensions.hpp`.
UIHost uses it on Windows; Wayland retains implicit button-drag capture. The existing EventResponse and non-cancellable cleanup events
keep their upstream semantics. Windows retained bitmap presentation borrows caller
pixels only for the call and copies them for repaint.

TextClipboard.hpp provides UI-thread SetClipboardText/RequestClipboardText helpers
using a Window's context. Text is valid UTF-8 without embedded NUL. Reads may complete
immediately and are cancelled when the native owner or context becomes unavailable.
Wayland uses the context's data device alongside URI drag/drop and requires an input
serial for writing. Portal dialogs retain the existing Window& owner and filter APIs;
until xdg-foreign parenting exists they enforce owner modality within LWS.

Wayland SetWindowIcon uses upstream WindowIcon values and the optional
xdg-toplevel-icon-v1 protocol. Unsupported compositors still return NotSupported;
UI headers provide an always-visible icon. No public bitmap enlargement API is added.

LWSUI::Event is the UI-layer notification type. Owned connections disconnect safely
on destruction and may outlive their publisher; dispatch retains stable callables and
new registrations start with the next notification. This isolates UI requirements from
the pinned LLUtils Event lifetime contract without changing that dependency.

### Context menus

LWSUI controls can supply flat command menus through `SetContextMenuProvider`.
Right-click or Shift+F10 requests the nearest provider on the target or its ancestors,
within the active popup. A registered provider returning no items suppresses the menu;
clearing the provider restores ancestor lookup. TextBox supplies Cut, Copy, Paste, and
Select All by default, including disabled editing actions for read-only text.

```cpp
control.SetContextMenuProvider([](LWSUI::Control&, const LWSUI::ContextMenuRequest&) {
    return std::vector<LWSUI::ContextMenuItem>{
        {"Refresh", "F5", [](LWSUI::Control& owner) { owner.Invalidate(); }},
        LWSUI::ContextMenuItem::Separator(),
        {"Unavailable action", "", {}, false}
    };
});
```

Providers run on request; item availability and optional `checked` states are snapshots.
Shortcut hints are descriptive, not key bindings, and checkmarks do not toggle themselves.
`UIHost::ShowContextMenu` and `CloseContextMenu` support programmatic invocation.
Menus retain editor focus and drafts, including inside a modal color picker. Up/Down,
Home/End, Enter/Space navigate and activate; Escape/Tab or an outside click dismiss.
Outside clicks are consumed. Commands execute through an owner-checked `Post` after
closing the menu; detached or unavailable owners are skipped. Callbacks must respect
the existing structural-edit lifetime rules and must not capture shorter-lived objects.
Long menus scroll using existing menu theme metrics. Submenus, icons, native menus,
and automatic shortcut registration are intentionally deferred.

### Menu-bar window controls

Horizontal Top/Bottom menu bars on top-level windows can opt into caption buttons
and native dragging. Other layouts retain their ordinary menu behavior.

```cpp
bar->SetWindowControls({
    .minimize = true,
    .maximize = true,
    .draggable = true,
    .requestClose = [&] { platform.RequestQuit(); }
});
```

The close callback enables the Close button and runs outside input dispatch. The
application decides whether to quit, hide the window, or decline closing. Minimize
and maximize/restore call `RequestShowState`; dragging unused bar space calls
`BeginWindowDrag(Move)` synchronously with the pointer press. Wayland Minimize is
visible but disabled. Defaults leave all window controls and dragging off.

For a resizable captionless window, use `WindowStyle::ResizableBorder` without
native caption or caption-button flags: the Win32 backend adds a caption for
`CloseButton`, `MinimizeButton`, and `MaximizeButton`. The controls-only showcase's
main Workspace demonstrates this configuration while its companion windows keep
native captions.

### Menu-bar icons and custom content

`MenuBar::SetIcon(BitmapSharedPtr)` adds an optional leading icon (left in a
horizontal bar, top in a vertical one). `Icon()` returns the retained bitmap;
passing null removes it and its space. The icon keeps its aspect ratio in a
24-logical-pixel box, clamped to the bar thickness. It starts a native window move
when window dragging is enabled for the bar. It is not a menu item and does not
change the native window icon.

`SetContent(std::unique_ptr<Control>, minimumWidth)` owns one optional control tree
between menu items and window buttons in horizontal Top/Bottom bars. `Content()`
returns that tree, and `SetContentMinimumWidth()` updates its minimum. The child
reports its preferred size through measurement. `MinimumWidth()` combines required
icon, menu, content, and window-button widths; `OnMinimumWidthChanged` lets the
application defer updates to native size limits. Structural content replacements
must be deferred from control callbacks, just like other tree mutations.

Content participates in focus, capture, Tab traversal, context menus, popups, and
edit completion. Its hit-test result identifies occupied regions; disabled controls
still occupy their rectangles. Containers that intend gaps to be draggable should
return null from HitTest for those gaps or passive text intended as a drag region.
Labels remain occupied unless their content container passes hits through; the
settings header does this for its title and saved-state indicator. Content
is clipped to its slot and never intercepts window buttons. Other bar layouts keep
the content attached but inactive, preserving control state for returning to a
horizontal dock. Existing bars without content retain their previous behavior.

The settings editor demonstrates this with its icon, title, theme selector, and
saved-state indicator. The bar remains generic and has no dependency on settings
documents, JSON, or theme persistence. A TODO in its layout code records the deferred
decision about reserving a guaranteed drag strip for combined-content bars.

### Theme presets

`LWSUI::MakeTheme(ThemePreset::Dark/Light/Warm)` returns a complete palette with the same
font and geometry defaults. Dark is the original default. Apply a palette through the
existing UIHost theme update path; close transient popups first so they reopen with the
new style. The controls-only demo's Palette button demonstrates live switching.
Settings-specific sections, JSON profiles, Save/Revert, and the F6 editor remain in
LWSSettings; LWSUI has no persistence or theme registry.

Popup replacement returns a success flag: callers must only use the new popup's controls
when OpenPopup succeeds. Rejected validation preserves the current popup, and window
resizing cancels transient popups through their normal rollback callback.
TextBox uses native point hit-testing for bidirectional text and measures only the active
caret and visual extent, avoiding an all-character caret map on every edit.

The Windows LWSLib target exports `UNICODE`, `_UNICODE`, and `NOMINMAX` as public
usage requirements, so core-only consumers see the same string ABI and compatible
Windows headers as the library. LWSUI inherits these requirements.

`TreeView::Row` is a fixed property row, constructed with owned name/editor controls
and an optional trailing action. Use NameControl(), EditorControl(), and ActionControl()
to access those roles. It no longer exposes Grid column configuration; measurement and
arrangement share one column calculation. As with other composites, callers must not
mutate the owned structure through a base-container cast. Derived controls retain the
protected capture helpers; host detachment/capture plumbing and handle lifetime records
are implementation-private. Logical focus may survive hiding/disabling a parent, but
input cannot reach that unavailable subtree, and Focus rejects foreign-host controls.

When LWSUI is enabled, LWSTests also covers event connection mutation and lifetime,
UTF-8 editing and validation, popup cancellation, deferred context-menu commands,
and scrolling without losing editor drafts. Core-only builds omit these UI tests.
