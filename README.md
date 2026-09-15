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

Shared code uses `Window`, `AnyEvent`, coherent `ClientAreaSize`, and logical client coordinates. `WindowConfig` and
`RequestClientSize()` always describe the drawable client area with `LogicalSize`; native title bars, borders, shadows,
and other outer decorations are deliberately outside the portable size contract. `ClientAreaSize::pixels` is a
non-convertible `PixelSize` and is authoritative for native rendering. `Scale()` derives the effective mapping between
the paired sizes.

Wayland scale is compositor-provided per-surface state, not physical-monitor DPI. With `wp_fractional_scale_v1` and
`wp_viewporter`, LWS allocates each pixel dimension with `ceil(logical * preferredScale)`, uses buffer scale one, and
sets the viewport destination to the logical size. Without those protocols, LWS uses the maximum integer scale of the
outputs containing the surface. A scale change that alters the paired pixel size publishes
`EventClientAreaSizeChanged` even when logical size is unchanged; clients must use that pixel size rather than monitor
scale or reconstructed dimensions. Platform
translation units may opt into typed extensions:

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
| Coherent client-area metrics | InvalidState | Available after configuration | Last configured metrics before native teardown | InvalidState |
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
