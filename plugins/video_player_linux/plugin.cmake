# SPDX-License-Identifier: Apache-2.0
# SPDX-FileCopyrightText: 2020-2026 Toyota Connected North America
#
# ivi-homescreen plugin manifest for the tcna-packages
# video_player_linux plugin. Consumed by ivi-homescreen's
# HomescreenPlugins.cmake / generated_plugin_registrant.cc pipeline.

set(PLUGIN                   "Video Player (Linux / GStreamer)")
set(PLUGIN_NAME              "video_player_linux")
set(PLUGIN_DESCRIPTION       "Flutter video_player backend for Linux using GStreamer")
set(PLUGIN_VERSION           "2.5.0")
set(PLUGIN_TARGET_NAME       "plugin_video_player_linux")
set(PLUGIN_HEADER            "video_player_linux/video_player_plugin_c_api.h")
set(PLUGIN_REGISTER_ENDPOINT "VideoPlayerLinuxPluginCApiRegisterWithRegistrar")
