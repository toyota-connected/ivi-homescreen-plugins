# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Audioplayers Linux")
set(PLUGIN_DESCRIPTION "An audioplayer plugin for Linux using GStreamer")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
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
    pkg_check_modules(GST IMPORTED_TARGET REQUIRED gstreamer-1.0>=1.4 gstreamer-audio-1.0)
endmacro()

set(PLUGIN_DATA_SOURCES
    audioplayers_linux_plugin_c_api.cc
    audioplayers_linux_plugin.cc
    messages.cc
    audio_player.cc
)

macro(PLUGIN_STEP_TARGETS)
    set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
    target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)
    target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
    target_include_directories(${PLUGIN_NAME} INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    include_directories(${CMAKE_CURRENT_SOURCE_DIR})
endmacro()

set(PLUGIN_DATA_LIBRARIES
    plugin_common
    plugin_common_glib
    PkgConfig::GST
)
