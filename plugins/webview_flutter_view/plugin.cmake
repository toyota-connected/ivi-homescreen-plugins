# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Webview Flutter View")
set(PLUGIN_DESCRIPTION "The Webview Flutter View plugin")

# "Camera Pipewire" -> "Camera_Pipewire"
string(REPLACE " " "_" underscore_fname "${PLUGIN_FULL_NAME}")
# "Camera_Pipewire" -> "camera_pipewire"
string(TOLOWER "${underscore_fname}" lcase_name)
# "Camera Pipewire" -> "CameraPipewire"
string(REPLACE " " "" camelcase_fname "${PLUGIN_FULL_NAME}")

set(PLUGIN_NAME "${lcase_name}")
set(PLUGIN_TARGET_NAME "plugin_${PLUGIN_NAME}")
set(PLUGIN_REGISTER_ENDPOINT "WebviewFlutterPluginCApiRegisterWithRegistrar")
set(PLUGIN_HEADER "${CMAKE_CURRENT_LIST_DIR}/include/${PLUGIN_NAME}/${PLUGIN_NAME}_plugin_c_api.h")


#
#  Step defines
#

# PLUGIN_STEP_DEPENDENCIES
#   Declare any dependencies that must be resolved before the plugin library is built.
#
macro(PLUGIN_STEP_DEPENDENCIES)
    if (NOT EXISTS ${CEF_ROOT})
        message(FATAL_ERROR "CEF_ROOT: \"${CEF_ROOT}\" does not exist")
    endif ()
    message(STATUS "  CEF_ROOT: ${CEF_ROOT}")
    
    if (NOT CEF_BUILD_TYPE)
        set(CEF_BUILD_TYPE "Release")
    endif ()
    message("CEF_BUILD_TYPE: ${CEF_BUILD_TYPE}")
    add_compile_definitions(CEF_ROOT=\"${CEF_ROOT}\")

    set(WEBVIEW_SUBPROCESS_PROJECT_NAME "webview_flutter_subprocess")

    add_subdirectory(${CEF_ROOT})
endmacro()

set(PLUGIN_DATA_SOURCES
    webview_flutter_view_plugin_c_api.cc
    webview_flutter_view_plugin.cc
    messages.g.cc
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_definitions(${PLUGIN_NAME} PUBLIC
        MESA_EGL_NO_X11_HEADERS
        WL_EGL_PLATFORM
        EGL_NO_X11
    )

    target_compile_options(${PLUGIN_NAME} PRIVATE
        -isystem${CEF_ROOT}
        -isystem${CEF_ROOT}/include
    )

    target_link_directories(${PLUGIN_NAME} PUBLIC
        ${CEF_ROOT}/${CEF_BUILD_TYPE}
    )

    add_dependencies(${PLUGIN_NAME} libcef_dll_wrapper)
endmacro()

set(PLUGIN_DATA_LIBRARIES
    libcef_dll_wrapper
    GLESv2
    EGL
)
