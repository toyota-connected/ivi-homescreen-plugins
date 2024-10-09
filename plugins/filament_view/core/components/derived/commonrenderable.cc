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
#include "commonrenderable.h"
#include "core/include/literals.h"
#include "core/utils/deserialize.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

CommonRenderable::CommonRenderable(const flutter::EncodableMap& params)
    : Component(std::string(__FUNCTION__)),
      m_bCullingOfObjectEnabled(true),
      m_bCastShadows(false),
      m_bReceiveShadows(false) {
  Deserialize::DecodeParameterWithDefault(
      kCullingEnabled, &m_bCullingOfObjectEnabled, params, true);
  Deserialize::DecodeParameterWithDefault(kReceiveShadows, &m_bReceiveShadows,
                                          params, false);
  Deserialize::DecodeParameterWithDefault(kCastShadows, &m_bCastShadows, params,
                                          false);
}

void CommonRenderable::DebugPrint(const std::string& tabPrefix) const {
  spdlog::debug(tabPrefix + "Culling Enabled: {}", m_bCullingOfObjectEnabled);
  spdlog::debug(tabPrefix + "Receive Shadows: {}", m_bReceiveShadows);
  spdlog::debug(tabPrefix + "Cast Shadows: {}", m_bCastShadows);
}

}  // namespace plugin_filament_view