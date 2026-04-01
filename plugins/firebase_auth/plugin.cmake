# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Firebase Auth")
set(PLUGIN_DESCRIPTION "The Firebase Auth plugin")

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
    if(NOT TARGET plugin_firebase_core)
        add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../firebase_core
                         ${CMAKE_CURRENT_BINARY_DIR}/_firebase_core)
    endif()
    set(CMAKE_THREAD_PREFER_PTHREAD ON)
    include(FindThreads)
endmacro()

set(PLUGIN_DATA_SOURCES
    firebase_auth_plugin.cc
    firebase_auth_plugin_c_api.cc
    messages.g.cc
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE INTERNAL_EXPERIMENTAL)
    target_link_directories(${PLUGIN_TARGET_NAME} PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
endmacro()

set(PLUGIN_DATA_LIBRARIES
    plugin_firebase_core
    firebase_sdk
)
