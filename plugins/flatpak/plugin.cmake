# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Flatpak")
set(PLUGIN_DESCRIPTION "The Flatpak plugin")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
set(PLUGIN_REGISTER_ENDPOINT "${camelcase_fname}PluginCApiRegisterWithRegistrar")
set(PLUGIN_HEADER "${CMAKE_CURRENT_LIST_DIR}/include/${PLUGIN_NAME}/${PLUGIN_NAME}_plugin_c_api.h")


#
#  Step defines
#

# PLUGIN_STEP_DEPENDENCIES
#   Declare any dependencies that must be resolved before the plugin library is built.
#
macro(PLUGIN_STEP_DEPENDENCIES)
    if(NOT TARGET plugin_common)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../common
                         ${CMAKE_CURRENT_BINARY_DIR}/_common)
    endif()
    pkg_check_modules(FLATPAK_DEPS IMPORTED_TARGET REQUIRED flatpak libxml-2.0 zlib)
    add_subdirectory(cache)
endmacro()

set(PLUGIN_DATA_SOURCES
    flatpak_plugin_c_api.cc
    flatpak_plugin.cc
    flatpak_shim.cc
    messages.g.cc
    appstream_catalog.cc
    component.cc
    icon.cc
    release.cc
    screenshot.cc
    portals/portal_manager.cc
    portals/portal_proxy.cc
    portals/permissions_portal/permissions_portal.cc
    portals/permissions_portal/permissions_portal.h
    operation_tracker.cc
    operation_tracker.h
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_options(${PLUGIN_NAME} PRIVATE
        -Wno-deprecated-declarations
    )

    if (BUILD_UNIT_TESTS)
        add_subdirectory(test)
    endif ()
endmacro()

set(PLUGIN_DATA_LIBRARIES
    PkgConfig::FLATPAK_DEPS
    plugin_common_curl
    plugin_common_glib
    plugin_flatpak_cache
)
