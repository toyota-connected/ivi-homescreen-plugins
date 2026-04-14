# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2020-2026 Toyota Connected North America
#
# ivi-homescreen-plugins integration shim.
#
# This file is intended to be copied (or symlinked) to
# `ivi-homescreen-plugins/plugins/video_player_linux/CMakeLists.txt`,
# replacing the old in-tree plugin. It forces VIDEO_PLAYER_EMBEDDER=ivi
# and add_subdirectory()s into the real tcna-packages tree, producing
# the static `plugin_video_player_linux` target that
# ivi-homescreen-plugins' `ENABLE_PLUGIN(video_player_linux)` macro
# expects.
#
# The one knob is VIDEO_PLAYER_LINUX_SOURCE_DIR, the path to the
# tcna-packages video_player_linux package. Defaults to a sibling
# layout: ivi-homescreen-plugins/ and tcna-packages/ as peers under
# the same workspace root.

set(VIDEO_PLAYER_EMBEDDER "ivi" CACHE STRING "" FORCE)

set(VIDEO_PLAYER_LINUX_SOURCE_DIR
    "${CMAKE_CURRENT_LIST_DIR}/../../../tcna-packages/packages/video_player/video_player_linux"
    CACHE PATH
    "Path to the tcna-packages video_player_linux package"
)

if(NOT EXISTS "${VIDEO_PLAYER_LINUX_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "[video_player_linux] VIDEO_PLAYER_LINUX_SOURCE_DIR "
        "(${VIDEO_PLAYER_LINUX_SOURCE_DIR}) does not contain a CMakeLists.txt. "
        "Set -DVIDEO_PLAYER_LINUX_SOURCE_DIR=/path/to/tcna-packages/packages/"
        "video_player/video_player_linux"
    )
endif()

add_subdirectory(
    "${VIDEO_PLAYER_LINUX_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/video_player_linux_build"
)
