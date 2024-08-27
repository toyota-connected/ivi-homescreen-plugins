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
#pragma once

#include <filament/math/vec3.h>
#include <filament/math/quat.h>
#include <flutter/encodable_value.h>

#include "core/scene/material/model/material.h"

namespace plugin_filament_view {

class Deserialize {
 public:
  Deserialize() = default;
  static ::filament::math::float3 Format3(const flutter::EncodableMap& map);
  static ::filament::math::quatf Format4(const flutter::EncodableMap& map);

  template <typename T>
  static void DecodeParameterWithDefault(const char* key, T* out_value, const flutter::EncodableMap& params, const T& default_value) {
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<T>(it->second)) {
          *out_value = std::get<T>(it->second);
      } else {
          *out_value = default_value;
      }
  }

  // Overload for enum types (e.g., ShapeType)
  template <typename T>
  static void DecodeEnumParameterWithDefault(const char* key, T* out_value, const flutter::EncodableMap& params, const T& default_value,
                                  std::enable_if_t<std::is_enum<T>::value>* = nullptr) {
      using UnderlyingType = typename std::underlying_type<T>::type;
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<UnderlyingType>(it->second)) {
          *out_value = static_cast<T>(std::get<UnderlyingType>(it->second));
      } else {
          *out_value = default_value;
      }
  }

  static void DecodeParameterWithDefault(const char* key, std::optional<std::unique_ptr<Material>>& out_value, const flutter::EncodableMap& params, const std::string& flutter_assets_path);
  static void DecodeParameterWithDefault(const char* key, filament::math::float3* out_value, const flutter::EncodableMap& params, const filament::math::float3& default_value);
  static void DecodeParameterWithDefault(const char* key, filament::math::quatf* out_value, const flutter::EncodableMap& params, const filament::math::quatf& default_value);

};
}  // namespace plugin_filament_view
