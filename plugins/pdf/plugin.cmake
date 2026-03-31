# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Pdf")
set(PLUGIN_DESCRIPTION "The Pdf plugin")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
set(PLUGIN_REGISTER_ENDPOINT "PrintingPluginCApiRegisterWithRegistrar")
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
endmacro()

set(PLUGIN_DATA_SOURCES
    pdf_plugin_c_api.cc
    pdf_plugin.cc
    libpdfium.cc
    messages.cc
)

macro(PLUGIN_STEP_TARGETS)
endmacro()

set(PLUGIN_DATA_LIBRARIES
    plugin_common
)
