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

#include <filament/math/quat.h>
#include <filament/math/vec3.h>
#include <flutter/encodable_value.h>

#include <core/components/derived/material_definitions.h>

namespace plugin_filament_view {

class Deserialize {
 public:
  Deserialize() = default;
  static ::filament::math::float3 Format3(const flutter::EncodableMap& map);
  static ::filament::math::quatf Format4(const flutter::EncodableMap& map);

  static const flutter::EncodableValue& DeserializeParameter(
      const char* key,
      const flutter::EncodableValue& value) {
    if (!std::holds_alternative<flutter::EncodableMap>(value)) {
      throw std::runtime_error("Provided value is not an EncodableMap");
    }

    const auto& params = std::get<flutter::EncodableMap>(value);
    auto it = params.find(flutter::EncodableValue(key));
    if (it != params.end()) {
      return it->second;
    }

    throw std::runtime_error("Key not found in EncodableMap");
  }

  template <typename T>
  static void DecodeParameterWithDefault(const char* key,
                                         T* out_value,
                                         const flutter::EncodableMap& params,
                                         const T& default_value) {
    auto it = params.find(flutter::EncodableValue(key));
    if (it != params.end() && std::holds_alternative<T>(it->second)) {
      *out_value = std::get<T>(it->second);
    } else {
      *out_value = default_value;
    }
  }

  // Overload for enum types (e.g., ShapeType)
  template <typename T>
  static void DecodeEnumParameterWithDefault(
      const char* key,
      T* out_value,
      const flutter::EncodableMap& params,
      const T& default_value,
      std::enable_if_t<std::is_enum<T>::value>* = nullptr) {
    using UnderlyingType = typename std::underlying_type<T>::type;
    auto it = params.find(flutter::EncodableValue(key));
    if (it != params.end() &&
        std::holds_alternative<UnderlyingType>(it->second)) {
      *out_value = static_cast<T>(std::get<UnderlyingType>(it->second));
    } else {
      *out_value = default_value;
    }
  }

  static void DecodeParameterWithDefault(
      const char* key,
      std::optional<std::unique_ptr<MaterialDefinitions>>& out_value,
      const flutter::EncodableMap& params);

  static void DecodeParameterWithDefault(
      const char* key,
      filament::math::float3* out_value,
      const flutter::EncodableMap& params,
      const filament::math::float3& default_value);

  static void DecodeParameterWithDefault(
      const char* key,
      filament::math::quatf* out_value,
      const flutter::EncodableMap& params,
      const filament::math::quatf& default_value);

  static void DecodeParameterWithDefault(const char* key,
                                         double* out_value,
                                         const flutter::EncodableMap& params,
                                         const double& default_value);

  static void DecodeParameterWithDefaultInt64(
      const char* key,
      int64_t* out_value,
      const flutter::EncodableMap& params,
      const int64_t& default_value);
};
}  // namespace plugin_filament_view
