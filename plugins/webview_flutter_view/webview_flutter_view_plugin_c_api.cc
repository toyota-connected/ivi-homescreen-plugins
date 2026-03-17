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

#include "include/webview_flutter_view/webview_flutter_view_plugin_c_api.h"

#include "flutter/plugin_registrar.h"

#include "webview_flutter_view_plugin.h"

void WebviewFlutterPluginCApiRegisterWithRegistrar(
    FlutterDesktopPluginRegistrar* registrar) {
  // Method channel setup
  plugin_webview_flutter::WebviewFlutterPlugin::RegisterWithRegistrar(
      flutter::PluginRegistrarManager::GetInstance()
          ->GetRegistrar<flutter::PluginRegistrar>(registrar));

  // Platform view setup
  registrar->engine->platform_views_handler->RegisterPlatformView(
      "plugins.flutter.io/webview",
      plugin_webview_flutter::WebviewFlutterPlugin::PlatformViewCreate);
}
