
find_package(PkgConfig REQUIRED)

add_library(plugin_common STATIC
        src/json/json_utils.cc
        src/time/time_tools.cc
        src/string/string_tools.cc
        src/uuid/uuidxx.cc
)
add_subdirectory(src/sdbus)
#  warning suppression
target_compile_options(sdbus-c++-objlib PRIVATE "-Wno-strict-prototypes" "-Wno-sign-conversion")

target_include_directories(plugin_common PUBLIC src include ${PROJECT_BINARY_DIR})
target_compile_definitions(plugin_common PUBLIC EGL_NO_X11)
target_link_libraries(plugin_common PUBLIC
    flutter
    toolchain::toolchain
    platform_homescreen
)
add_sanitizers(plugin_common)

pkg_check_modules(CURL IMPORTED_TARGET libcurl)
if (CURL_FOUND)
    add_library(plugin_common_curl STATIC src/curl_client/curl_client.cc)
    target_include_directories(plugin_common_curl PUBLIC src include ${PROJECT_BINARY_DIR})
    target_link_libraries(plugin_common_curl PUBLIC
        PkgConfig::CURL
        platform_homescreen
        toolchain::toolchain
    )
endif ()

pkg_check_modules(GLIB IMPORTED_TARGET glib-2.0)
if (GLIB_FOUND)
    add_library(plugin_common_glib STATIC src/glib/main_loop.cc)
    target_include_directories(plugin_common_glib PUBLIC src include ${PROJECT_BINARY_DIR})
    target_link_libraries(plugin_common_glib PUBLIC PkgConfig::GLIB toolchain::toolchain)
    add_sanitizers(plugin_common_glib)
endif ()

if (BUILD_UNIT_TESTS)
    add_subdirectory(src/curl_client/test)
endif ()