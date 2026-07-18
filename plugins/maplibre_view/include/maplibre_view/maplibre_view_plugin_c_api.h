/*
 * Copyright 2026 Toyota Connected North America
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

#ifndef FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_C_API_H_
#define FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_C_API_H_

#include "flutter_homescreen.h"  // FlutterDesktopEngineRef

#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))

#if defined(__cplusplus)
extern "C" {
#endif

// Installs the MapLibre platform-view factory into the engine's
// PlatformViewRegistry for viewType "views/maplibre-view". Unlike the legacy
// platform-view plugins (constructed on demand by the generated dispatch), this
// view uses the registry factory path: the registry owns each created instance
// and destroys it on dispose. Called once at startup from
// PluginsApiRegisterPlugins.
FLUTTER_PLUGIN_EXPORT void MapLibreViewPluginRegisterFactory(
    FlutterDesktopEngineRef engine);

#if defined(__cplusplus)
}  // extern "C"
#endif

#endif  // FLUTTER_PLUGIN_MAPLIBRE_VIEW_PLUGIN_C_API_H_
