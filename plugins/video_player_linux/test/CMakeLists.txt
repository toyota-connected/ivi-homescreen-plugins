#
# Copyright 2024 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Test harness for video_player_linux.
#
# Only added to the build when BUILD_VIDEO_PLAYER_LINUX_TESTS=ON (see the
# parent CMakeLists). The media corpus under ./media is fetched on demand by
# the fetch_video_player_test_media target; media files are never committed
# to the repository.
#

find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(VIDEO_PLAYER_LINUX_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(VIDEO_PLAYER_LINUX_TEST_MEDIA_DIR "${VIDEO_PLAYER_LINUX_TEST_DIR}/media")
set(VIDEO_PLAYER_LINUX_FETCH_SCRIPT
        "${VIDEO_PLAYER_LINUX_TEST_DIR}/scripts/fetch_media.py")

# On-demand fetch target. Not tied to ALL — must be invoked explicitly,
# e.g. `cmake --build . --target fetch_video_player_test_media`.
add_custom_target(fetch_video_player_test_media
        COMMAND ${Python3_EXECUTABLE} "${VIDEO_PLAYER_LINUX_FETCH_SCRIPT}"
        WORKING_DIRECTORY "${VIDEO_PLAYER_LINUX_TEST_MEDIA_DIR}"
        COMMENT "Fetching video_player_linux test media corpus (manifest.json)"
        VERBATIM)

# Convenience target that fetches + pins sha256 back into the manifest.
add_custom_target(fetch_video_player_test_media_learn
        COMMAND ${Python3_EXECUTABLE} "${VIDEO_PLAYER_LINUX_FETCH_SCRIPT}"
                --learn
        WORKING_DIRECTORY "${VIDEO_PLAYER_LINUX_TEST_MEDIA_DIR}"
        COMMENT "Fetching + learning sha256 hashes"
        VERBATIM)

# Real playback tests land in later phases and depend on the fetched
# corpus. For the 0.3 harness we register two harness-level ctests that
# don't require any media to be fetched; playback tests in later phases
# will gate themselves on file existence so ctest runs gracefully in
# environments without the corpus. add_test only registers when the
# top-level build called enable_testing() (BUILD_UNIT_TESTS=ON).
add_test(
        NAME video_player_linux_media_manifest_parses
        COMMAND ${Python3_EXECUTABLE} -c
                "import json, sys; json.load(open(sys.argv[1]))"
                "${VIDEO_PLAYER_LINUX_TEST_MEDIA_DIR}/manifest.json"
)

add_test(
        NAME video_player_linux_fetcher_help
        COMMAND ${Python3_EXECUTABLE}
                "${VIDEO_PLAYER_LINUX_FETCH_SCRIPT}" --help
)
