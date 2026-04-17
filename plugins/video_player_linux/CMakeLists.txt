#
# Copyright 2023-2024 Toyota Connected North America
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

set(PROJECT_NAME "video_player_linux")

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "plugin_${PROJECT_NAME}")

add_library(${PLUGIN_NAME} STATIC
        video_player_plugin_c_api.cc
        video_player_plugin.cc
        video_player.cc
        messages.g.cc
        backend_registry.cc
        backend_generic_v4l2.cc
        backend_imx8m.cc
        config.cc
        platform_detection.cc
)
set_target_properties(${PLUGIN_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_features(${PLUGIN_NAME} PRIVATE cxx_std_17)

# System-level dependencies.
find_package(PkgConfig REQUIRED)
pkg_check_modules(GST IMPORTED_TARGET REQUIRED
        gstreamer-1.0>=1.4
        gstreamer-video-1.0
        gstreamer-audio-1.0
        gstreamer-tag-1.0
        gstreamer-pbutils-1.0
        gstreamer-app-1.0
        gstreamer-allocators-1.0)
pkg_check_modules(UDEV IMPORTED_TARGET REQUIRED libudev)
# GLESv2 + EGL must be linked from this static lib so the symbols are
# resolved after libplugin_video_player_linux.a in the final executable
# link line. Without this, --as-needed strips them when they appear
# earlier than the plugin .a in the homescreen link command.
pkg_check_modules(GLES IMPORTED_TARGET REQUIRED egl glesv2)
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_include_directories(${PLUGIN_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PUBLIC platform_homescreen flutter PkgConfig::GST PkgConfig::UDEV PkgConfig::GLES plugin_common_glib)

# ─────────────────────────────────────────────────────────────────────────────
# Phase 0.3 — Optional test media corpus + test harness.
#
# Off by default. When ON, adds `fetch_video_player_test_media` (and a
# `_learn` variant) custom targets plus ctest entries under test/. Media
# files themselves are NOT committed — see test/media/manifest.json for
# source URLs and test/.gitignore for the exclusion rules.
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_VIDEO_PLAYER_LINUX_TESTS
        "Enable fetch target + ctest harness for video_player_linux" OFF)

if (BUILD_VIDEO_PLAYER_LINUX_TESTS)
    add_subdirectory(test)
endif ()

# ─────────────────────────────────────────────────────────────────────────────
# Optional Pigeon regeneration target.
#
# The Pigeon-generated messages.g.{h,cc} files in this directory are owned
# by the upstream Dart package at tcna-packages/packages/video_player/
# video_player_linux/. Regenerating them is a multi-step process:
#
#   1. Run `dart run pigeon` against pigeons/messages.dart in the Dart pkg
#   2. clang-format the generated linux_cpp/messages.{h,cpp} output
#   3. Patch a few Pigeon-emitted constructs that fail this repo's
#      clang-tidy gate (push_back → emplace_back, by-value string param
#      → const ref)
#   4. Copy the result to messages.g.{h,cc} in this directory
#   5. Patch the cpp #include from "messages.h" to "messages.g.h"
#
# Off by default — only opted into when the Dart-side IDL has actually
# changed. Requires `dart`, `clang-format`, and `sed` on PATH, plus the
# tcna-packages checkout next to ivi-homescreen-plugins (override the
# path with -DVIDEO_PLAYER_LINUX_PIGEON_DIR=...).
# ─────────────────────────────────────────────────────────────────────────────
option(BUILD_PLUGIN_VIDEO_PLAYER_LINUX_PIGEON_REGEN
        "Add a 'regen_video_player_linux_pigeon' make target that re-runs Pigeon"
        OFF)

if (BUILD_PLUGIN_VIDEO_PLAYER_LINUX_PIGEON_REGEN)
    set(VIDEO_PLAYER_LINUX_PIGEON_DIR
            "${CMAKE_CURRENT_SOURCE_DIR}/../../../tcna-packages/packages/video_player/video_player_linux"
            CACHE PATH
            "Path to the tcna-packages video_player_linux Dart package")

    find_program(DART_EXECUTABLE dart)
    # Prefer clang-format-18 because that's what the CI lint job runs;
    # different major versions of clang-format wrap long parameter lists
    # differently and using a newer one locally produces diffs that CI
    # then rejects. Fall back to plain `clang-format` if -18 isn't on
    # PATH (the developer is responsible for ensuring it matches CI).
    # Unset the cache var first so re-runs pick up the explicit version
    # preference even if a previous configure cached a different binary.
    unset(CLANG_FORMAT_EXECUTABLE CACHE)
    find_program(CLANG_FORMAT_EXECUTABLE
            NAMES clang-format-18 clang-format)
    find_program(SED_EXECUTABLE sed)

    if (NOT DART_EXECUTABLE OR NOT CLANG_FORMAT_EXECUTABLE OR NOT SED_EXECUTABLE)
        message(WARNING
                "[video_player_linux] regen_video_player_linux_pigeon target "
                "requires dart, clang-format and sed on PATH; target disabled.")
    elseif (NOT EXISTS "${VIDEO_PLAYER_LINUX_PIGEON_DIR}/pigeons/messages.dart")
        message(WARNING
                "[video_player_linux] VIDEO_PLAYER_LINUX_PIGEON_DIR "
                "(${VIDEO_PLAYER_LINUX_PIGEON_DIR}) does not contain "
                "pigeons/messages.dart; regen target disabled.")
    else()
        set(_pigeon_dart "${VIDEO_PLAYER_LINUX_PIGEON_DIR}")
        set(_pigeon_h    "${_pigeon_dart}/linux_cpp/messages.h")
        set(_pigeon_cpp  "${_pigeon_dart}/linux_cpp/messages.cpp")
        set(_plugin_h    "${CMAKE_CURRENT_SOURCE_DIR}/messages.g.h")
        set(_plugin_cc   "${CMAKE_CURRENT_SOURCE_DIR}/messages.g.cc")

        add_custom_target(regen_video_player_linux_pigeon
                COMMENT "Regenerating Pigeon bindings for video_player_linux"

                # 1. Run pigeon in the Dart package directory.
                COMMAND ${DART_EXECUTABLE} run pigeon
                        --input pigeons/messages.dart
                WORKING_DIRECTORY "${_pigeon_dart}"

                # 2. clang-format the Dart-side copy under THAT package's
                #    .clang-format (Google / 80 col).
                COMMAND ${CLANG_FORMAT_EXECUTABLE} -i
                        "${_pigeon_h}" "${_pigeon_cpp}"

                # 3. Patch Pigeon-emitted constructs that trip clang-tidy:
                #    push_back(EncodableValue(x))  → emplace_back(x)
                #    push_back(EncodableValue())   → emplace_back()
                #    const std::string channel_name → const std::string&
                COMMAND ${SED_EXECUTABLE} -i
                        -e "s|wrapped\\.push_back(EncodableValue())|wrapped.emplace_back()|g"
                        -e "s|wrapped\\.push_back(EncodableValue(\\(.*\\)))|wrapped.emplace_back(\\1)|g"
                        -e "s|FlutterError CreateConnectionError(const std::string channel_name)|FlutterError CreateConnectionError(const std::string\\& channel_name)|g"
                        "${_pigeon_cpp}"

                # 4. Copy the result over the plugin's messages.g.{h,cc}.
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_pigeon_h}" "${_plugin_h}"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        "${_pigeon_cpp}" "${_plugin_cc}"

                # 5. Fix the cpp #include — Pigeon emits "messages.h" but
                #    the plugin needs "messages.g.h" to avoid a clash with
                #    GStreamer's <gst/gstmessage.h>.
                COMMAND ${SED_EXECUTABLE} -i
                        -e "s|^#include \"messages.h\"|#include \"messages.g.h\"|"
                        "${_plugin_cc}"

                # 6. Re-format the destination files under the plugin
                #    repo's own .clang-format (Chromium / 100 col). The
                #    Dart package and plugin repo use different styles,
                #    so the same file content has to be formatted twice.
                COMMAND ${CLANG_FORMAT_EXECUTABLE} -i
                        "${_plugin_h}" "${_plugin_cc}"

                VERBATIM)
    endif()
endif()
