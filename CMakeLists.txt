cmake_minimum_required(VERSION 3.20) #TODO: 4.2 creates a build error
project("LWS")

set(CMAKE_CXX_STANDARD 26)

option(LWS_BUILD_EXAMPLES "Build LWS example executable" ${PROJECT_IS_TOP_LEVEL})
option(LWS_BUILD_TESTS "Build LWS tests" ${PROJECT_IS_TOP_LEVEL})
option(LWS_BUILD_SHARED "Build LWSLib as a shared library" OFF)

if(NOT TARGET LLUtils::LLUtils)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/LLUtils")
endif()

# ---------------------------------------------------------------------------
# Platform detection
# ---------------------------------------------------------------------------
if(WIN32)
  set(LWS_PLATFORM_WIN32 ON)
elseif(UNIX)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
  find_package(Wayland QUIET)
  if(Wayland_FOUND)
    message(STATUS "Wayland found: ${Wayland_CLIENT_LIBRARY}")
    set(LWS_PLATFORM_WAYLAND ON)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(CaptionText REQUIRED IMPORTED_TARGET pangocairo fontconfig)
    pkg_check_modules(TextInput REQUIRED IMPORTED_TARGET xkbcommon dbus-1)

    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
    find_path(WAYLAND_PROTOCOLS_DIRECTORY
        NAMES stable/xdg-shell/xdg-shell.xml
        PATHS /usr/share/wayland-protocols
        REQUIRED
    )

    set(LWS_WAYLAND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/wayland")
    # Every protocol uses the same scanner commands and publishes its generated outputs once.
    function(lws_generate_protocol name xml stem)
      set(header "${LWS_WAYLAND_GENERATED_DIR}/${stem}-client-protocol.h")
      set(source "${LWS_WAYLAND_GENERATED_DIR}/${stem}-protocol.c")
      foreach(mode client-header private-code)
        if(mode STREQUAL "client-header")
          set(output "${header}")
        else()
          set(output "${source}")
        endif()
        add_custom_command(OUTPUT "${output}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
          COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" ${mode} "${xml}" "${output}"
          DEPENDS "${xml}" VERBATIM)
      endforeach()
      set(LWS_${name}_XML "${xml}" PARENT_SCOPE)
      set(LWS_${name}_HEADER "${header}" PARENT_SCOPE)
      set(LWS_${name}_SOURCE "${source}" PARENT_SCOPE)
      set(LWS_WAYLAND_GENERATED_FILES ${LWS_WAYLAND_GENERATED_FILES} "${header}" "${source}" PARENT_SCOPE)
    endfunction()
    lws_generate_protocol(XDG_SHELL "${WAYLAND_PROTOCOLS_DIRECTORY}/stable/xdg-shell/xdg-shell.xml" xdg-shell)
    lws_generate_protocol(XDG_DECORATION "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml" xdg-decoration)
    lws_generate_protocol(RELATIVE_POINTER "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/relative-pointer/relative-pointer-unstable-v1.xml" relative-pointer)
    lws_generate_protocol(POINTER_CONSTRAINTS "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml" pointer-constraints)
    lws_generate_protocol(FRACTIONAL_SCALE "${WAYLAND_PROTOCOLS_DIRECTORY}/staging/fractional-scale/fractional-scale-v1.xml" fractional-scale-v1)
    lws_generate_protocol(VIEWPORTER "${WAYLAND_PROTOCOLS_DIRECTORY}/stable/viewporter/viewporter.xml" viewporter)
  else()
    message(WARNING "Wayland not found: building the unavailable X11 scaffold")
    set(LWS_PLATFORM_X11 ON)
  endif()
endif()

# ---------------------------------------------------------------------------
# Shared include/compile settings helper function
# ---------------------------------------------------------------------------
function(lws_configure_target target)
  target_include_directories(${target}
    PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
    PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/src/LWS/include"
  )
  target_link_libraries(${target} PUBLIC LLUtils::LLUtils)

  if(LWS_PLATFORM_WIN32)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_WIN32 LWS_HAS_WIN32_BACKEND NOMINMAX UNICODE _UNICODE)
    # Keep library names lowercase for case-sensitive MinGW builds on Linux.
    target_link_libraries(${target} PRIVATE
        ole32
        shell32
        user32
        gdi32
        comctl32
    )
  elseif(LWS_PLATFORM_WAYLAND)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_WAYLAND LWS_HAS_WAYLAND_BACKEND)
  elseif(LWS_PLATFORM_X11)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_X11)
  endif()
endfunction()

# ---------------------------------------------------------------------------
# LWS library used by the main executable and tests
# ---------------------------------------------------------------------------
file(GLOB_RECURSE lwsLibSources
    "./src/LWS/source/*.cpp"
    "./src/LWS/include/*.hpp"
)
# Exclude example from the library
list(FILTER lwsLibSources EXCLUDE REGEX ".*Examples.*")
if(LWS_PLATFORM_WIN32)
  list(FILTER lwsLibSources EXCLUDE REGEX ".*/Wayland/.*")
elseif(LWS_PLATFORM_WAYLAND)
  list(FILTER lwsLibSources EXCLUDE REGEX ".*/Win32/.*")
elseif(LWS_PLATFORM_X11)
  list(FILTER lwsLibSources EXCLUDE REGEX ".*/Win32/.*")
  list(FILTER lwsLibSources EXCLUDE REGEX ".*/Wayland/.*")
endif()

if(LWS_BUILD_SHARED)
  add_library(LWSLib SHARED ${lwsLibSources})
  # Export the compiled API on Windows without changing the public headers.
  set_target_properties(LWSLib PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
else()
  add_library(LWSLib STATIC ${lwsLibSources})
endif()
if(LWS_PLATFORM_WAYLAND)
  # Optional: older wayland-protocols packages still build with header icons only.
  set(LWS_TOPLEVEL_ICON_XML "${WAYLAND_PROTOCOLS_DIRECTORY}/staging/xdg-toplevel-icon/xdg-toplevel-icon-v1.xml")
  if(EXISTS "${LWS_TOPLEVEL_ICON_XML}")
    lws_generate_protocol(TOPLEVEL_ICON "${LWS_TOPLEVEL_ICON_XML}" xdg-toplevel-icon-v1)
    target_compile_definitions(LWSLib PRIVATE LWS_HAS_TOPLEVEL_ICON)
  endif()
  target_sources(LWSLib PRIVATE ${LWS_WAYLAND_GENERATED_FILES})
  target_include_directories(LWSLib PRIVATE "${LWS_WAYLAND_GENERATED_DIR}")
  target_link_libraries(LWSLib PRIVATE Wayland::Client PkgConfig::CaptionText PkgConfig::TextInput)
  if(TARGET Wayland::Cursor)
    target_link_libraries(LWSLib PRIVATE Wayland::Cursor)
    target_compile_definitions(LWSLib PRIVATE LWS_HAS_WAYLAND_CURSOR)
  endif()
endif()
lws_configure_target(LWSLib)

# ---------------------------------------------------------------------------
# Main executable
# ---------------------------------------------------------------------------
if(LWS_BUILD_EXAMPLES)
  add_executable(LWS "./src/LWS/source/Examples/example.cpp")
  target_link_libraries(LWS PRIVATE LWSLib)
  lws_configure_target(LWS)
  if(MSVC)
    target_link_options(LWS PRIVATE /SUBSYSTEM:CONSOLE)
  endif()
endif()

# ---------------------------------------------------------------------------
# Tests (Catch2 via FetchContent)
# ---------------------------------------------------------------------------
if(LWS_BUILD_TESTS)
  if(NOT TARGET Catch2::Catch2WithMain)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)
  endif()

  file(GLOB_RECURSE testSources CONFIGURE_DEPENDS "./tests/*.cpp")
  if(testSources)
    add_executable(LWSTests ${testSources})
    target_link_libraries(LWSTests PRIVATE LWSLib Catch2::Catch2WithMain)
    lws_configure_target(LWSTests)
    if(MSVC)
      target_link_options(LWSTests PRIVATE /SUBSYSTEM:CONSOLE)
    endif()

    if(LWS_PLATFORM_WAYLAND)
      target_include_directories(LWSTests PRIVATE "${LWS_WAYLAND_GENERATED_DIR}")
      target_link_libraries(LWSTests PRIVATE Wayland::Client PkgConfig::TextInput)
      pkg_check_modules(WaylandServer QUIET IMPORTED_TARGET wayland-server)
      if(TARGET PkgConfig::WaylandServer)
        target_link_libraries(LWSTests PRIVATE PkgConfig::WaylandServer)
        # Fixtures need their own protocol definitions because the library hides them.
        target_sources(LWSTests PRIVATE
            "${LWS_XDG_SHELL_SOURCE}"
            "${LWS_FRACTIONAL_SCALE_SOURCE}"
            "${LWS_VIEWPORTER_SOURCE}"
            "${LWS_POINTER_CONSTRAINTS_SOURCE}"
            "${LWS_RELATIVE_POINTER_SOURCE}"
        )
        target_compile_definitions(LWSTests PRIVATE LWS_TEST_WAYLAND_SERVER)
        target_include_directories(LWSTests PRIVATE "${LWS_WAYLAND_GENERATED_DIR}")
        foreach(protocol XDG_SHELL FRACTIONAL_SCALE VIEWPORTER)
          set(server_header "${LWS_WAYLAND_GENERATED_DIR}/test-${protocol}-server.h")
          add_custom_command(OUTPUT "${server_header}"
            COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" server-header "${LWS_${protocol}_XML}" "${server_header}"
            DEPENDS "${LWS_${protocol}_XML}" VERBATIM)
          target_sources(LWSTests PRIVATE "${server_header}")
        endforeach()
      else()
        message(STATUS "Wayland protocol tests disabled: wayland-server development package unavailable")
      endif()
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(LWSTests)
  endif()
endif()

# Reusable controls; no settings or JSON dependency.
set(LWS_UI_SUPPORTED OFF)
if(WIN32 OR LWS_PLATFORM_WAYLAND)
    set(LWS_UI_SUPPORTED ON)
endif()
option(LWS_BUILD_UI "Build the reusable LWSUI library" ${LWS_UI_SUPPORTED})
option(LWSUI_BUILD_DEMO "Build the controls-only demo" ${PROJECT_IS_TOP_LEVEL})
if(LWS_BUILD_UI)
    add_library(LWSUI STATIC
        src/LWSUI/source/Control.cpp src/LWSUI/source/UIHost.cpp
        src/LWSUI/source/GridModel.cpp src/LWSUI/source/GridView.cpp src/LWSUI/source/GridEditors.cpp
        src/LWSUI/source/GridViewRender.cpp src/LWSUI/source/GridViewInput.cpp
        src/LWSUI/source/Primitives.cpp src/LWSUI/source/TextBox.cpp src/LWSUI/source/Slider.cpp src/LWSUI/source/Containers.cpp src/LWSUI/source/Layout.cpp
        src/LWSUI/source/Composites.cpp src/LWSUI/source/Menu.cpp src/LWSUI/source/MenuWindowControls.cpp src/LWSUI/source/MenuContent.cpp)
    # Keep core-only consumers from building unused controls. Linking LWSUI still
    # builds it normally; standalone builds continue to include the library.
    if(NOT PROJECT_IS_TOP_LEVEL)
        set_target_properties(LWSUI PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
    add_library(LWSUI::LWSUI ALIAS LWSUI)
    target_include_directories(LWSUI PUBLIC src/LWSUI/include)
    target_link_libraries(LWSUI PUBLIC LWSLib LLUtils::LLUtils)
    target_compile_features(LWSUI PUBLIC cxx_std_26)
    if(WIN32)
        target_sources(LWSUI PRIVATE src/LWSUI/source/CanvasWin32.cpp)
        target_link_libraries(LWSUI PRIVATE d2d1 dwrite windowscodecs)
    elseif(LWS_PLATFORM_WAYLAND)
        target_sources(LWSUI PRIVATE src/LWSUI/source/CanvasWayland.cpp)
        target_link_libraries(LWSUI PRIVATE PkgConfig::CaptionText)
    else()
        message(FATAL_ERROR "LWSUI requires Windows or Wayland")
    endif()
    if(TARGET LWSTests)
        target_compile_definitions(LWSTests PRIVATE LWS_TEST_UI)
        target_link_libraries(LWSTests PRIVATE LWSUI::LWSUI)
        target_include_directories(LWSTests PRIVATE src/LWSUI)
    endif()
    if(LWSUI_BUILD_DEMO)
        add_executable(LWSUIGridDemo src/LWSUI/demo/GridDemo.cpp)
        target_link_libraries(LWSUIGridDemo PRIVATE LWSUI::LWSUI)
        function(lwsui_configure_demo_executable target)
            if(WIN32)
                set_target_properties(${target} PROPERTIES WIN32_EXECUTABLE TRUE)
                if(MSVC)
                    target_link_options(${target} PRIVATE /ENTRY:mainCRTStartup)
                elseif(MINGW)
                    target_link_options(${target} PRIVATE -Wl,--entry,mainCRTStartup)
                endif()
            endif()
        endfunction()
        lwsui_configure_demo_executable(LWSUIGridDemo)
        if(TARGET LWSTests)
            add_test(NAME LWSUIGridDemoSmoke COMMAND $<TARGET_FILE:LWSUIGridDemo> --smoke)
            set_tests_properties(LWSUIGridDemoSmoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
        endif()
        add_executable(LWSUIScrollDemo src/LWSUI/demo/ScrollDemo.cpp)
        target_link_libraries(LWSUIScrollDemo PRIVATE LWSUI::LWSUI)
        lwsui_configure_demo_executable(LWSUIScrollDemo)
        if(TARGET LWSTests)
            add_test(NAME LWSUIScrollDemoSmoke COMMAND $<TARGET_FILE:LWSUIScrollDemo> --smoke)
            set_tests_properties(LWSUIScrollDemoSmoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
        endif()
        add_executable(LWSUISimpleExample src/LWSUI/examples/simple.cpp)
        target_link_libraries(LWSUISimpleExample PRIVATE LWSUI::LWSUI)
        lwsui_configure_demo_executable(LWSUISimpleExample)
        if(TARGET LWSTests)
            add_test(NAME LWSUISimpleExampleSmoke COMMAND $<TARGET_FILE:LWSUISimpleExample> --smoke)
            set_tests_properties(LWSUISimpleExampleSmoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
        endif()
        if(LWS_PLATFORM_WIN32)
            add_executable(LWSUISnappingDemo src/LWSUI/demo/SnappingDemo.cpp)
            target_link_libraries(LWSUISnappingDemo PRIVATE LWSUI::LWSUI dwmapi)
            lwsui_configure_demo_executable(LWSUISnappingDemo)
            if(TARGET LWSTests)
                add_test(NAME LWSUISnappingDemoSmoke COMMAND $<TARGET_FILE:LWSUISnappingDemo> --smoke)
                set_tests_properties(LWSUISnappingDemoSmoke PROPERTIES TIMEOUT 20 RUN_SERIAL TRUE)
            endif()
        endif()
        add_executable(LWSUIDemo src/LWSUI/demo.cpp src/LWSUI/demo/MenuGallery.cpp)
        lwsui_configure_demo_executable(LWSUIDemo)
        add_library(LWSUIDemoSupport STATIC src/LWSUI/demo/Showcase.cpp src/LWSUI/demo/SplitPreview.cpp)
        target_link_libraries(LWSUIDemoSupport PUBLIC LWSUI::LWSUI)
        target_link_libraries(LWSUIDemo PRIVATE LWSUIDemoSupport)
        if(TARGET LWSTests)
            target_link_libraries(LWSTests PRIVATE LWSUIDemoSupport)
            target_compile_definitions(LWSTests PRIVATE LWS_TEST_SHOWCASE)
        endif()
    endif()
endif()
