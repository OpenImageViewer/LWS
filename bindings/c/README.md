# C bindings for LWS and LWSUI

The wrappers build and link against both libraries on Windows. A C executable
exercises them at runtime. Generated code uses the supplied SWIG 4.5.1 executable.

## Build and verify

Run from PowerShell:

```powershell
& D:/dev/LWS/bindings/c/Build-And-Test.ps1
```

This regenerates the wrappers, configures CMake, builds the static library and
runs three tests. It requires CMake and Visual Studio 18 with the ClangCL toolset.
ClangCL is used because this project's CMake targets require C++26, which CMake
does not currently advertise for the installed MSVC compiler. The generated
wrapper also passes a separate MSVC C++ syntax check with `/Zc:__cplusplus`.

The default output is `build/c-bindings-clang/Debug/LWSC.lib`. The build script
accepts `-Configuration Release` and `-BuildDirectory`. Only Debug was verified.
Use the `LWSC` CMake target to inherit the required dependencies and definitions.
It links LWSUI, LWSLib, LLUtils and the Windows system libraries.

Pass `-Shared` to build and test against `LWSLib.dll` instead of static LWSLib:

```powershell
& ./bindings/c/Build-And-Test.ps1 -Shared
```

That configures `LWS_BUILD_SHARED=ON` and uses `build/c-bindings-shared` by default.
The smoke executable receives a copy of the DLL beside it. The C wrapper and LWSUI
remain static, while their core LWS calls cross the DLL boundary.

To generate files without building:

```powershell
& ./bindings/c/Generate-Wrappers.ps1
```

The generator accepts `-SwigPath` and `-OutputDirectory`. Files use CRLF.

## Hello World GUI in C

`hello_world.c` is an interactive Windows example based on LWSUISimpleExample.
It displays a greeting, a name field, and a Say hello button in a resizable window.
Closing the window ends the message loop and releases its host and controls.
The example accepts no parameters and contains no automated test code.

Build it with `Build-And-Test.ps1`, then run:

```powershell
& ./build/c-bindings-clang/Debug/lws_c_hello_world.exe
```

Run that command from the repository root. The automated GUI smoke test is a
separate source file, `hello_world_smoke.c`, and executable,
`lws_c_hello_world_smoke`. It checks visibility, layout, a routed button click,
the greeting text, and the normal close-request path. CTest runs it automatically.
## Files and C usage

Include `lws_c_support.h` for the generated API and the ownership helpers.
`lws_c_wrap.h` contains the generated declarations. Compile `lws_c_wrap.cxx`
and `lws_c_support.cpp` as C++. Compile callers as C. For static linkage, define
`STATIC_LINKED`, as the provided CMake target does.

`smoke.c` is the separate C binding smoke test. It verifies geometry and enum arguments,
button text and state, narrow and wide strings, event callbacks and disconnection,
control ownership transfer, move-only posted tasks, successful and failed listener
results, and a hidden native window's creation and teardown. `event_dispatch_test.cpp`
verifies the updated LLUtils event dispatch, including predicate termination,
ordinary dispatch, disconnection and argument propagation.

Ownership rules:

- Delete standalone objects with their generated delete functions.
- Free returned narrow and wide strings with `lws_c_string_free` and
  `lws_c_wstring_free`. Returned strings are allocated copies.
- Event getters return borrowed publishers. Do not delete them.
- Retain callback user data until the task executes or the connection disconnects.
- `lws_c_button_take` adopts a standalone button exactly once. The caller must stop
  deleting the raw button. Passing its owner to Add or SetRoot consumes the owned
  control. Delete the empty owner handle afterward. Remove returns a new owner.
- Delete posted callback handles after the generated call consumes their callable.
- Listener result objects own their connections. Their connection getters are
  borrowed. Deleting a result disconnects the registration.
- Preserve the libraries' context-thread and window/host lifetime requirements.

## Reproducible generation fixes

`ownership.i` maps move-only arguments, moves returned values, and prevents
invalid default construction of expected results. It maps C++ strings to C
strings, including the native Windows wide strings.

`Generate-Wrappers.ps1` creates dependency-ordered `lws_c.i` and parser-only
header copies under `parser/`. Those copies simplify syntax SWIG cannot parse.
The implementation always includes the original headers. Do not add `parser/`
to the compiler include path.

The experimental C backend has two additional defects handled by narrow generation
fixes. It drops readonly metadata when copying inherited event members, so the
script removes noncopyable event setters from both source and header. It also
qualifies enum references without qualifying their definitions, so the script
resolves those references against the generated enum definitions.

The updated LLUtils submodule provides `Event::RaiseWhile` in both event modes.
The event test uses its `Subscribe` and `Unsubscribe` API. No local dependency
patch is required.

## Scope and limitations

This configuration targets Win32 and excludes Wayland-only extension functions.
SWIG still omits nested classes, unsupported operators and templates without
explicit instantiation, as recorded in `generation.log`.

Some advanced standard-library types remain opaque. The supplied support API
covers tasks, zero-argument UI events, control ownership, close requests, and window listener results.
Other opaque containers and result types need typed adapters before a C client
can construct, inspect or dispose of them. This is a verified C binding for the
covered operations, rather than complete conversion of every C++ API to idiomatic C.
String mappings are NUL-terminated and do not preserve embedded NUL bytes.

Validation output is in `static-verification.log` and `shared-verification.log`.
