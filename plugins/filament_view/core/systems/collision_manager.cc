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
#include "collision_manager.h"

#include "plugins/common/common.h"

namespace plugin_filament_view {

CollisionManager::CollisionManager() {}

void CollisionManager::DebugPrint() {
  spdlog::debug("CollisionManager Debug Info:");
}

}  // namespace plugin_filament_view
