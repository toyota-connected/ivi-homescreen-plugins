# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Camera Pipewire")
set(PLUGIN_DESCRIPTION "A camera plugin for Linux using Pipewire")

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
    pkg_check_modules(GLIB IMPORTED_TARGET REQUIRED glib-2.0)
    pkg_check_modules(PIPEWIRE IMPORTED_TARGET REQUIRED libpipewire-0.3)
    pkg_check_modules(JPEG IMPORTED_TARGET REQUIRED libjpeg)
endmacro()

set(PLUGIN_DATA_SOURCES
    camera_pipewire_plugin_c_api.cc
    camera_plugin.cc
    messages.g.cc
    camera_stream.cc
    camera_stream.h
    pipewire_graph.cc
    pipewire_graph.h
)

macro(PLUGIN_STEP_TARGETS)
endmacro()

set(PLUGIN_DATA_LIBRARIES
    PkgConfig::GLIB
    PkgConfig::PIPEWIRE
    PkgConfig::JPEG
    plugin_common
)
