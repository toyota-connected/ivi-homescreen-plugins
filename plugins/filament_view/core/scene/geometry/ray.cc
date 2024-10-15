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
#include "ray.h"

#include <core/include/literals.h>
#include <core/utils/deserialize.h>

namespace plugin_filament_view {

Ray::Ray(const flutter::EncodableMap& params)
    : direction_({0, 0, -1}), position_({0, 0, 0}), length_(1.0f) {
  Deserialize::DecodeParameterWithDefault(kDirection, &direction_, params,
                                          filament::math::float3(0, 0, -1));
  Deserialize::DecodeParameterWithDefault(kStartingPosition, &position_, params,
                                          filament::math::float3(0, 0, 0));
  Deserialize::DecodeParameterWithDefault(kLength, &length_, params, 1.0f);
}

}  // namespace plugin_filament_view
