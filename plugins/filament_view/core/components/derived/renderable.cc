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
#include "renderable.h"

#include <core/entity/base/entityobject.h>
#include <core/include/literals.h>
#include <core/utils/deserialize.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
Renderable::Renderable(const flutter::EncodableMap& params)
  : Component(std::string(__FUNCTION__)),
    allowCulling(true),
    receiveShadows(false),
    castShadows(false) {
  Deserialize::DecodeParameterWithDefault(kCullingEnabled, &allowCulling, params, true);
  Deserialize::DecodeParameterWithDefault(kReceiveShadows, &receiveShadows, params, false);
  Deserialize::DecodeParameterWithDefault(kCastShadows, &castShadows, params, false);
}

////////////////////////////////////////////////////////////////////////////
void Renderable::debugPrint(const std::string& tabPrefix) const {
  spdlog::debug(tabPrefix + "Culling Enabled: {}", allowCulling);
  spdlog::debug(tabPrefix + "Receive Shadows: {}", receiveShadows);
  spdlog::debug(tabPrefix + "Cast Shadows: {}", castShadows);
}

AABB Renderable::getAABB() const {
  // Create a 1x1x1 box
  AABB box;
  box.center = {0.0f, 0.0f, 0.0f};
  box.halfExtent = {0.5f, 0.5f, 0.5f};

  spdlog::trace(
    "[{}] has AABB.scale: x={}, y={}, z={}", __FUNCTION__,  //
    box.halfExtent.x * 2,                                   //
    box.halfExtent.y * 2,                                   //
    box.halfExtent.z * 2
  );

  return box;
}

}  // namespace plugin_filament_view
