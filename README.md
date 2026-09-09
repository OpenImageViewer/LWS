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
    return platform.Shutdown() == LWS::Result::Success ? 0 : 1;
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

Typed native handles are borrowed and stable from successful window creation until destruction begins. Destroy every
dependent swap chain, EGL surface, `wl_egl_window`, or registered native callback before destroying the window.

## Repository layout

| Path | Purpose |
| --- | --- |
| `src/LWS/include/LWS` | Portable public headers and typed extension headers |
| `src/LWS/source` | Portable implementation and private native backends |
| `tests` | Unit and platform integration tests |
