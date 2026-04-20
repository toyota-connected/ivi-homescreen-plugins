/*
 * Copyright 2020-2023 Toyota Connected North America
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef PLUGINS_VIDEO_PLAYER_LINUX_INCLUDE_VIDEO_PLAYER_LINUX_VIDEO_PLAYER_PLUGIN_C_API_H_
#define PLUGINS_VIDEO_PLAYER_LINUX_INCLUDE_VIDEO_PLAYER_LINUX_VIDEO_PLAYER_PLUGIN_C_API_H_

#include <cstdint>
#include <string>
#include <vector>

#include <flutter_plugin_registrar.h>

#include "flutter_homescreen.h"
#include "platform_view_listener.h"

#ifdef FLUTTER_PLUGIN_IMPL
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#else
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

#if defined(__cplusplus)
extern "C" {
#endif

FLUTTER_PLUGIN_EXPORT void VideoPlayerLinuxPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrar* registrar);

// Platform-view entry point. Called by the embedder's platform-views
// dispatcher when Dart instantiates a PlatformViewLink with
// viewType == "@views/video-player". Constructs a VideoPlayerView that
// multi-inherits PlatformView + ICompositorSurface, decodes asset/uri
// from the standard-codec `params` blob, and builds its underlying
// VideoPlayer through the process-wide VideoPlayerPlugin instance.
FLUTTER_PLUGIN_EXPORT void VideoPlayerLinuxPluginCApiPlatformViewCreate(
    FlutterDesktopPluginRegistrar* registrar,
    int32_t id,
    std::string viewType,
    int32_t direction,
    double top,
    double left,
    double width,
    double height,
    const std::vector<uint8_t>& params,
    const std::string& assetDirectory,
    FlutterDesktopEngineRef engine,
    PlatformViewAddListener add_listener,
    PlatformViewRemoveListener remove_listener,
    void* platform_views_context);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // PLUGINS_VIDEO_PLAYER_LINUX_INCLUDE_VIDEO_PLAYER_LINUX_VIDEO_PLAYER_PLUGIN_C_API_H_
