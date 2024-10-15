/*
 * Copyright 2020-2024 Toyota Connected North America
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

#include <include/filament_view/filament_view_plugin_c_api.h>

#include <flutter/plugin_registrar.h>

#include <core/systems/ecsystems_manager.h>
#include <filament_view_plugin.h>

void FilamentViewPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar,
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
    PlatformViewAddListener addListener,
    PlatformViewRemoveListener removeListener,
    void* platform_view_context) {
  plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar),
      id, std::move(viewType), direction, top, left, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);

  // Uncomment if you want two views.
  /*plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar),
      id, std::move(viewType), direction, top, 1200, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);

  plugin_filament_view::FilamentViewPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar),
      id, std::move(viewType), direction, top, 800, width, height, params,
      assetDirectory, engine, addListener, removeListener,
      platform_view_context);*/

  // after we're done doing setup, kick off the run loops
  auto ecsManager = plugin_filament_view::ECSystemManager::GetInstance();
  if (ecsManager->getRunState() ==
      plugin_filament_view::ECSystemManager::RunState::Initialized) {
    ecsManager->DebugPrint();
    ecsManager->StartRunLoop();
  }
}
