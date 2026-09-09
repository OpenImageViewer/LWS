cmake_minimum_required(VERSION 3.20) #TODO: 4.2 creates a build error
project("LWS")

set(CMAKE_CXX_STANDARD 26)

option(LWS_BUILD_EXAMPLES "Build LWS example executable" ${PROJECT_IS_TOP_LEVEL})
option(LWS_BUILD_TESTS "Build LWS tests" ${PROJECT_IS_TOP_LEVEL})

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

    find_program(WAYLAND_SCANNER_EXECUTABLE NAMES wayland-scanner REQUIRED)
    find_path(WAYLAND_PROTOCOLS_DIRECTORY
        NAMES stable/xdg-shell/xdg-shell.xml
        PATHS /usr/share/wayland-protocols
        REQUIRED
    )

    set(LWS_WAYLAND_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/wayland")
    set(LWS_XDG_SHELL_XML "${WAYLAND_PROTOCOLS_DIRECTORY}/stable/xdg-shell/xdg-shell.xml")
    set(LWS_XDG_SHELL_HEADER "${LWS_WAYLAND_GENERATED_DIR}/xdg-shell-client-protocol.h")
    set(LWS_XDG_SHELL_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/xdg-shell-protocol.c")
    set(LWS_XDG_DECORATION_XML
        "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml")
    set(LWS_XDG_DECORATION_HEADER "${LWS_WAYLAND_GENERATED_DIR}/xdg-decoration-client-protocol.h")
    set(LWS_XDG_DECORATION_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/xdg-decoration-protocol.c")
    set(LWS_RELATIVE_POINTER_XML
        "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/relative-pointer/relative-pointer-unstable-v1.xml")
    set(LWS_RELATIVE_POINTER_HEADER "${LWS_WAYLAND_GENERATED_DIR}/relative-pointer-client-protocol.h")
    set(LWS_RELATIVE_POINTER_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/relative-pointer-protocol.c")
    set(LWS_POINTER_CONSTRAINTS_XML
        "${WAYLAND_PROTOCOLS_DIRECTORY}/unstable/pointer-constraints/pointer-constraints-unstable-v1.xml")
    set(LWS_POINTER_CONSTRAINTS_HEADER "${LWS_WAYLAND_GENERATED_DIR}/pointer-constraints-client-protocol.h")
    set(LWS_POINTER_CONSTRAINTS_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/pointer-constraints-protocol.c")
    set(LWS_FRACTIONAL_SCALE_XML
        "${WAYLAND_PROTOCOLS_DIRECTORY}/staging/fractional-scale/fractional-scale-v1.xml")
    set(LWS_FRACTIONAL_SCALE_HEADER "${LWS_WAYLAND_GENERATED_DIR}/fractional-scale-v1-client-protocol.h")
    set(LWS_FRACTIONAL_SCALE_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/fractional-scale-v1-protocol.c")
    set(LWS_VIEWPORTER_XML "${WAYLAND_PROTOCOLS_DIRECTORY}/stable/viewporter/viewporter.xml")
    set(LWS_VIEWPORTER_HEADER "${LWS_WAYLAND_GENERATED_DIR}/viewporter-client-protocol.h")
    set(LWS_VIEWPORTER_SOURCE "${LWS_WAYLAND_GENERATED_DIR}/viewporter-protocol.c")

    add_custom_command(
        OUTPUT "${LWS_XDG_SHELL_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${LWS_XDG_SHELL_XML}" "${LWS_XDG_SHELL_HEADER}"
        DEPENDS "${LWS_XDG_SHELL_XML}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${LWS_XDG_SHELL_SOURCE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${LWS_XDG_SHELL_XML}" "${LWS_XDG_SHELL_SOURCE}"
        DEPENDS "${LWS_XDG_SHELL_XML}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${LWS_XDG_DECORATION_HEADER}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${LWS_XDG_DECORATION_XML}"
                "${LWS_XDG_DECORATION_HEADER}"
        DEPENDS "${LWS_XDG_DECORATION_XML}"
        VERBATIM
    )
    add_custom_command(
        OUTPUT "${LWS_XDG_DECORATION_SOURCE}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
        COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${LWS_XDG_DECORATION_XML}"
                "${LWS_XDG_DECORATION_SOURCE}"
        DEPENDS "${LWS_XDG_DECORATION_XML}"
        VERBATIM
    )
    foreach(protocol RELATIVE_POINTER POINTER_CONSTRAINTS FRACTIONAL_SCALE VIEWPORTER)
      add_custom_command(
          OUTPUT "${LWS_${protocol}_HEADER}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
          COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" client-header "${LWS_${protocol}_XML}"
                  "${LWS_${protocol}_HEADER}"
          DEPENDS "${LWS_${protocol}_XML}"
          VERBATIM
      )
      add_custom_command(
          OUTPUT "${LWS_${protocol}_SOURCE}"
          COMMAND "${CMAKE_COMMAND}" -E make_directory "${LWS_WAYLAND_GENERATED_DIR}"
          COMMAND "${WAYLAND_SCANNER_EXECUTABLE}" private-code "${LWS_${protocol}_XML}"
                  "${LWS_${protocol}_SOURCE}"
          DEPENDS "${LWS_${protocol}_XML}"
          VERBATIM
      )
    endforeach()
  else()
    message(WARNING "Wayland not found — building the unavailable X11 scaffold")
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
      "${CMAKE_CURRENT_SOURCE_DIR}/external/LLUtils/include"
  )
  target_link_libraries(${target} PUBLIC LLUtils::LLUtils)
  target_compile_definitions(${target} PRIVATE NOMINMAX UNICODE _UNICODE)

  if(LWS_PLATFORM_WIN32)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_WIN32 LWS_HAS_WIN32_BACKEND)
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
# LWS library (static) — shared between main exe and tests
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

add_library(LWSLib STATIC ${lwsLibSources})
if(LWS_PLATFORM_WAYLAND)
  target_sources(LWSLib PRIVATE
      "${LWS_XDG_SHELL_HEADER}"
      "${LWS_XDG_SHELL_SOURCE}"
      "${LWS_XDG_DECORATION_HEADER}"
      "${LWS_XDG_DECORATION_SOURCE}"
      "${LWS_RELATIVE_POINTER_HEADER}"
      "${LWS_RELATIVE_POINTER_SOURCE}"
      "${LWS_POINTER_CONSTRAINTS_HEADER}"
      "${LWS_POINTER_CONSTRAINTS_SOURCE}"
      "${LWS_FRACTIONAL_SCALE_HEADER}"
      "${LWS_FRACTIONAL_SCALE_SOURCE}"
      "${LWS_VIEWPORTER_HEADER}"
      "${LWS_VIEWPORTER_SOURCE}"
  )
  target_include_directories(LWSLib PRIVATE "${LWS_WAYLAND_GENERATED_DIR}")
  target_link_libraries(LWSLib PRIVATE Wayland::Client PkgConfig::CaptionText)
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

  file(GLOB_RECURSE testSources "./tests/*.cpp")
  if(testSources)
    add_executable(LWSTests ${testSources})
    target_link_libraries(LWSTests PRIVATE LWSLib Catch2::Catch2WithMain)
    lws_configure_target(LWSTests)
    if(MSVC)
      target_link_options(LWSTests PRIVATE /SUBSYSTEM:CONSOLE)
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(LWSTests)
  endif()
endif()
