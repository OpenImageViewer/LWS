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
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
  # find_package(Wayland QUIET PATHS /usr/lib/cmake/Wayland)
  find_package(Wayland QUIET)
  if(Wayland_FOUND)
    message(STATUS "Wayland found: ${Wayland_LIBRARIES}")
    set(LWS_PLATFORM_WAYLAND ON)
  else()
    message(WARNING "Wayland not found — falling back to X11")
    find_package(X11 REQUIRED)
    set(LWS_PLATFORM_X11 ON)
  endif()
endif()

# ---------------------------------------------------------------------------
# Shared include/compile settings helper function
# ---------------------------------------------------------------------------
function(lws_configure_target target)
  target_include_directories(${target} PUBLIC
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/LWS/include"
      "${CMAKE_CURRENT_SOURCE_DIR}/external/LLUtils/include"
  )
  target_link_libraries(${target} PUBLIC LLUtils::LLUtils)
  target_compile_definitions(${target} PRIVATE NOMINMAX UNICODE _UNICODE)

  if(LWS_PLATFORM_WIN32)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_WIN32)
    target_link_libraries(${target} PRIVATE
        Ole32
        Shell32
        Shcore
        User32
        Gdi32
        ComCtl32
    )
  elseif(LWS_PLATFORM_WAYLAND)
    target_compile_definitions(${target} PUBLIC LWS_PLATFORM_WAYLAND)
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
  list(FILTER lwsLibSources EXCLUDE REGEX ".*/Wayland/(CursorBackendWayland|WindowBackendWayland|WindowWayland)\\.cpp$")
endif()

add_library(LWSLib STATIC ${lwsLibSources})
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
      target_link_options(LWSTests PRIVATE /ENTRY:wmainCRTStartup)
    endif()

    include(CTest)
    include(Catch)
    catch_discover_tests(LWSTests)
  endif()
endif()
