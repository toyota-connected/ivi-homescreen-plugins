#
# Copyright 2023 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

include_guard()

if (NOT EXISTS ${LIBWEBRTC_INC_DIR})
    message(FATAL_ERROR "LIBWEBRTC_INC_DIR: \"${LIBWEBRTC_INC_DIR}\" does not exist")
endif ()

if (NOT EXISTS ${LIBWEBRTC_LIB})
    message(FATAL_ERROR "LIBWEBRTC_LIB: \"${LIBWEBRTC_LIB}\" does not exist")
endif ()

message(STATUS "  LIBWEBRTC_INC_DIR: ${LIBWEBRTC_INC_DIR}")
message(STATUS "  LIBWEBRTC_LIB: ${LIBWEBRTC_LIB}")

add_library(plugin_webrtc STATIC
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

target_compile_definitions(plugin_webrtc PRIVATE -DRTC_DESKTOP_DEVICE)

target_compile_options(plugin_webrtc PRIVATE -isystem${CMAKE_CURRENT_SOURCE_DIR}/third_party/svpng)

target_include_directories(plugin_webrtc PRIVATE
        include
        third_party/flutter-webrtc/common/cpp/include
)

# mask third party header warnings
target_compile_options(plugin_webrtc PRIVATE -isystem${LIBWEBRTC_INC_DIR})

target_link_libraries(plugin_webrtc PUBLIC
        flutter
        platform_homescreen
        ${LIBWEBRTC_LIB}
)
