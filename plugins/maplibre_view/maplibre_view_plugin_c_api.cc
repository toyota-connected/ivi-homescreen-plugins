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

#include "include/maplibre_view/maplibre_view_plugin_c_api.h"

#include "flutter_desktop_engine_state.h"
#include "logging/logging.h"
#include "maplibre_view_plugin.h"
#include "platform_views/platform_view_registry.h"

void MapLibreViewPluginRegisterFactory(FlutterDesktopEngineRef engine) {
  if (engine == nullptr || !engine->platform_view_registry) {
    return;
  }
  // The factory captures the engine so it can reach the live view (compositor
  // surface + Vulkan device) at create time. The engine state owns the
  // registry, so it outlives every factory invocation.
  engine->platform_view_registry->RegisterFactory(
      "views/maplibre-view",
      [engine](PlatformViewRegistry& registry,
               const PlatformViewRegistry::CreateRequest& request) {
        return plugin_maplibre_view::MapLibreViewPlugin::Create(
            engine, registry, request);
      });
  ihs::log::debug(
      "[maplibre] factory registered for viewType 'views/maplibre-view'");
}
