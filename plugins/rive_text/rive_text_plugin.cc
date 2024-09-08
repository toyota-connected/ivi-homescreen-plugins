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

#include "rive_text_plugin.h"

#include <flutter/plugin_registrar.h>

#include "librive_text.h"
#include "plugins/common/common.h"

namespace plugin_rive_text {

// static
void RiveTextPlugin::RegisterWithRegistrar(
    flutter::PluginRegistrar* /* registrar */) {
  if (!LibRiveText::IsPresent()) {
    spdlog::error("librive_text.so not found");
  }
}

RiveTextPlugin::RiveTextPlugin() = default;

RiveTextPlugin::~RiveTextPlugin() = default;

}  // namespace plugin_rive_text