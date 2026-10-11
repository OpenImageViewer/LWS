cmake_minimum_required(VERSION 3.20)
project(LWSCBindings LANGUAGES C CXX)

set(LWS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LWS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LWSUI_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(LWS_BUILD_UI ON CACHE BOOL "" FORCE)
add_subdirectory(../.. lws)

add_library(LWSC STATIC lws_c_wrap.cxx lws_c_support.cpp)
target_link_libraries(LWSC PUBLIC LWSUI::LWSUI)
target_include_directories(LWSC PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(LWSC PUBLIC STATIC_LINKED)
target_compile_definitions(LWSC PRIVATE _CRT_NONSTDC_NO_WARNINGS)
if(MSVC)
    target_compile_options(LWSC PRIVATE /EHsc /Zc:__cplusplus)
endif()

add_executable(lws_c_smoke smoke.c)
target_link_libraries(lws_c_smoke PRIVATE LWSC)
set_target_properties(lws_c_smoke PROPERTIES LINKER_LANGUAGE CXX)
if(WIN32 AND LWS_BUILD_SHARED)
    add_custom_command(TARGET lws_c_smoke POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:LWSLib>" "$<TARGET_FILE_DIR:lws_c_smoke>"
        VERBATIM)
endif()
enable_testing()
add_test(NAME lws_c_smoke COMMAND lws_c_smoke)
set_tests_properties(lws_c_smoke PROPERTIES TIMEOUT 30)

add_executable(lws_event_dispatch_test event_dispatch_test.cpp)
target_link_libraries(lws_event_dispatch_test PRIVATE LLUtils::LLUtils)
add_test(NAME lws_event_dispatch_test COMMAND lws_event_dispatch_test)
