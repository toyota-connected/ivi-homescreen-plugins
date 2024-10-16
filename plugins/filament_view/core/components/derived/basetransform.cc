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
#include "basetransform.h"

#include <core/include/literals.h>
#include <core/utils/deserialize.h>
#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
BaseTransform::BaseTransform(const flutter::EncodableMap& params)
    : Component(std::string(__FUNCTION__)),
      m_f3CenterPosition(0, 0, 0),
      m_f3ExtentsSize(0, 0, 0),
      m_f3Scale(1, 1, 1),
      m_quatRotation(0, 0, 0, 1) {
  Deserialize::DecodeParameterWithDefault(kSize, &m_f3ExtentsSize, params,
                                          filament::math::float3(0, 0, 0));
  Deserialize::DecodeParameterWithDefault(kCenterPosition, &m_f3CenterPosition,
                                          params,
                                          filament::math::float3(0, 0, 0));
  Deserialize::DecodeParameterWithDefault(kScale, &m_f3Scale, params,
                                          filament::math::float3(1, 1, 1));
  Deserialize::DecodeParameterWithDefault(kRotation, &m_quatRotation, params,
                                          filament::math::quatf(0, 0, 0, 1));
}

////////////////////////////////////////////////////////////////////////////
void BaseTransform::DebugPrint(const std::string& tabPrefix) const {
  spdlog::debug(tabPrefix + "Center Position: x={}, y={}, z={}",
                m_f3CenterPosition.x, m_f3CenterPosition.y,
                m_f3CenterPosition.z);
  spdlog::debug(tabPrefix + "Scale: x={}, y={}, z={}", m_f3Scale.x, m_f3Scale.y,
                m_f3Scale.z);
  spdlog::debug(tabPrefix + "Rotation: x={}, y={}, z={} w={}", m_quatRotation.x,
                m_quatRotation.y, m_quatRotation.z, m_quatRotation.w);
  spdlog::debug(tabPrefix + "Extents Size: x={}, y={}, z={}", m_f3ExtentsSize.x,
                m_f3ExtentsSize.y, m_f3ExtentsSize.z);
}

}  // namespace plugin_filament_view