# NOTE: `PLUGIN_FULL_NAME` must follow the format of "Xx Yy..." where words are captialized and space-separated. This is because it is used in generated code and documentation where this formatting is expected.
set(PLUGIN_FULL_NAME "Webrtc")
set(PLUGIN_DESCRIPTION "The Webrtc plugin")

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
    if (NOT EXISTS ${LIBWEBRTC_INC_DIR})
        message(FATAL_ERROR "LIBWEBRTC_INC_DIR: \"${LIBWEBRTC_INC_DIR}\" does not exist")
    endif ()

    if (NOT EXISTS ${LIBWEBRTC_LIB})
        message(FATAL_ERROR "LIBWEBRTC_LIB: \"${LIBWEBRTC_LIB}\" does not exist")
    endif ()

    message(STATUS "  LIBWEBRTC_INC_DIR: ${LIBWEBRTC_INC_DIR}")
    message(STATUS "  LIBWEBRTC_LIB: ${LIBWEBRTC_LIB}")
endmacro()

set(PLUGIN_DATA_SOURCES
    webrtc_plugin.cc
    webrtc_plugin_c_api.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_common.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_data_channel.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_frame_capturer.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_frame_cryptor.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_media_stream.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_peerconnection.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_screen_capture.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_video_renderer.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_webrtc.cc
    third_party/flutter-webrtc/common/cpp/src/flutter_webrtc_base.cc
)

macro(PLUGIN_STEP_TARGETS)
    target_compile_definitions(${PLUGIN_TARGET_NAME} PRIVATE -DRTC_DESKTOP_DEVICE)
    target_compile_options(${PLUGIN_TARGET_NAME} PRIVATE
        -isystem${CMAKE_CURRENT_SOURCE_DIR}/third_party/svpng
    )
    target_include_directories(${PLUGIN_TARGET_NAME} PRIVATE
        third_party/flutter-webrtc/common/cpp/include
    )

    # mask third party header warnings
    target_compile_options(${PLUGIN_TARGET_NAME} PRIVATE
        -isystem${LIBWEBRTC_INC_DIR}
    )
endmacro()

set(PLUGIN_DATA_LIBRARIES
    ${LIBWEBRTC_LIB}
)
