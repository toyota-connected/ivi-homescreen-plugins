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
#include "collidable.h"
#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

Collidable::Collidable(const flutter::EncodableMap& params)
    : Component(std::string(__FUNCTION__)) {
  Deserialize::DecodeParameterWithDefault(
      kCollidableExtents, &m_f3ExtentsSize, params,
      filament::math::float3(1.0f, 1.0f, 1.0f));

  // Deserialize the static flag, defaulting to 'true'
  Deserialize::DecodeParameterWithDefault(kCollidableIsStatic, &isStatic,
                                          params, true);

  // Deserialize the collision layer, defaulting to 0
  Deserialize::DecodeParameterWithDefaultInt64(kCollidableLayer,
                                               &collisionLayer, params, 0);

  // Deserialize the collision mask, defaulting to 0xFFFFFFFF
  Deserialize::DecodeParameterWithDefaultInt64(kCollidableMask, &collisionMask,
                                               params, 0xFFFFFFFFu);

  // Deserialize the flag for matching attached objects, defaulting to 'false'
  Deserialize::DecodeParameterWithDefault(kCollidableShouldMatchAttachedObject,
                                          &shouldMatchAttachedObject, params,
                                          false);

  // Deserialize the shape type, defaulting to some default ShapeType (replace
  // ShapeType::Default with your actual default)
  Deserialize::DecodeEnumParameterWithDefault(kCollidableShapeType, &shapeType_,
                                              params, ShapeType::Cube);
}

void Collidable::DebugPrint(const std::string tabPrefix) const {
  spdlog::debug(tabPrefix + "Collidable Debug Info:");

  // Log whether the object is static
  spdlog::debug(tabPrefix + "Is Static: {}", isStatic);

  // Log the collision layer and mask
  spdlog::debug(tabPrefix + "Collision Layer: {}", collisionLayer);
  spdlog::debug(tabPrefix + "Collision Mask: 0x{:X}", collisionMask);

  // Log the flag for whether it should match the attached object
  spdlog::debug(tabPrefix + "Should Match Attached Object: {}",
                shouldMatchAttachedObject);

  // Log the shape type (you can modify this to log the enum name if needed)
  spdlog::debug(tabPrefix + "Shape Type: {}",
                static_cast<int>(shapeType_));  // assuming ShapeType is an enum

  // Log the extents size (x, y, z)
  spdlog::debug(tabPrefix + "Extents Size: x={}, y={}, z={}", m_f3ExtentsSize.x,
                m_f3ExtentsSize.y, m_f3ExtentsSize.z);
}

}  // namespace plugin_filament_view