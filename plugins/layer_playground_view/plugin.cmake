# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Layer Playground View")
set(PLUGIN_DESCRIPTION "The Layer Playground View plugin")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
set(PLUGIN_REGISTER_ENDPOINT "LayerPlaygroundPluginCApiRegisterWithRegistrar")
set(PLUGIN_HEADER "${CMAKE_CURRENT_LIST_DIR}/include/${PLUGIN_NAME}/${PLUGIN_NAME}_plugin_c_api.h")


#
#  Step defines
#

# PLUGIN_STEP_DEPENDENCIES
#   Declare any dependencies that must be resolved before the plugin library is built.
#
macro(PLUGIN_STEP_DEPENDENCIES)
    pkg_check_modules(WAYLAND_EGL REQUIRED IMPORTED_TARGET wayland-egl)
endmacro()

set(PLUGIN_DATA_SOURCES
    layer_playground_view_plugin.cc
    layer_playground_view_plugin_c_api.cc
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_definitions(${PLUGIN_TARGET_NAME} PUBLIC
        MESA_EGL_NO_X11_HEADERS
        WL_EGL_PLATFORM
        EGL_NO_X11
    )
    target_include_directories(${PLUGIN_TARGET_NAME} PUBLIC .)
endmacro()

set(PLUGIN_DATA_LIBRARIES
    PkgConfig::WAYLAND_EGL
    GLESv2
    EGL
)
