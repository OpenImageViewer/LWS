# Find Wayland and create imported targets

find_path(Wayland_CLIENT_INCLUDE_DIR wayland-client.h)
find_library(Wayland_CLIENT_LIBRARY wayland-client)

find_path(Wayland_SERVER_INCLUDE_DIR wayland-server.h)
find_library(Wayland_SERVER_LIBRARY wayland-server)

find_path(Wayland_CURSOR_INCLUDE_DIR wayland-cursor.h)
find_library(Wayland_CURSOR_LIBRARY wayland-cursor)

find_path(Wayland_EGL_INCLUDE_DIR wayland-egl.h)
find_library(Wayland_EGL_LIBRARY wayland-egl)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Wayland DEFAULT_MSG
    Wayland_CLIENT_INCLUDE_DIR
    Wayland_CLIENT_LIBRARY
)

if(Wayland_FOUND)

    if(NOT TARGET Wayland::Client)
        add_library(Wayland::Client UNKNOWN IMPORTED)
        set_target_properties(Wayland::Client PROPERTIES
            IMPORTED_LOCATION "${Wayland_CLIENT_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Wayland_CLIENT_INCLUDE_DIR}"
        )
    endif()

    if(Wayland_SERVER_LIBRARY AND NOT TARGET Wayland::Server)
        add_library(Wayland::Server UNKNOWN IMPORTED)
        set_target_properties(Wayland::Server PROPERTIES
            IMPORTED_LOCATION "${Wayland_SERVER_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Wayland_SERVER_INCLUDE_DIR}"
        )
    endif()

    if(Wayland_CURSOR_LIBRARY AND NOT TARGET Wayland::Cursor)
        add_library(Wayland::Cursor UNKNOWN IMPORTED)
        set_target_properties(Wayland::Cursor PROPERTIES
            IMPORTED_LOCATION "${Wayland_CURSOR_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Wayland_CURSOR_INCLUDE_DIR}"
        )
    endif()

    if(Wayland_EGL_LIBRARY AND NOT TARGET Wayland::Egl)
        add_library(Wayland::Egl UNKNOWN IMPORTED)
        set_target_properties(Wayland::Egl PROPERTIES
            IMPORTED_LOCATION "${Wayland_EGL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${Wayland_EGL_INCLUDE_DIR}"
        )
    endif()

endif()
