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

#include <core/components/derived/material.h>

#include <plugins/common/common.h>
#include <utility>

namespace plugin_filament_view {

class Deserialize {
  public:
    Deserialize() = default;
    static ::filament::math::float3 Format3(const flutter::EncodableMap& map);
    static ::filament::math::quatf Format4(const flutter::EncodableMap& map);

    /// @returns true if the given key exists in the map and is not null.
    static bool HasKey(const flutter::EncodableMap& params, const char* key) {
      const auto it = params.find(flutter::EncodableValue(key));
      return it != params.end() && !it->second.IsNull();
    }

    /// @brief Decode a parameter from the given map.
    /// @throws std::runtime_error if the parameter is not found or is of the
    /// wrong type.
    template<typename T>
    static T DecodeParameter(const char* key, const flutter::EncodableMap& params) {
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<T>(it->second)) {
        return std::get<T>(it->second);
      } else {
        throw std::runtime_error(fmt::format("Parameter '{}' not found or wrong type", key));
      }
    }

    /// @brief Decode an optional parameter from the given map.
    /// @returns std::optional<T> containing the value if found, or std::nullopt
    /// if not.
    template<typename T>
    static std::optional<T> DecodeOptionalParameter(
      const char* key,
      const flutter::EncodableMap& params
    ) {
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<T>(it->second)) {
        return std::get<T>(it->second);
      } else {
        return std::nullopt;
      }
    }

    /*
     * Decoders with default values
     */

    /// @returns true if the parameter was found and set to out_value,
    ///          false if it was not found and out_value was set to default_value.
    template<typename T>
    static bool DecodeParameterWithDefault(
      const char* key,
      T* out_value,
      const flutter::EncodableMap& params,
      const T& default_value
    ) {
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<T>(it->second)) {
        *out_value = std::get<T>(it->second);
        return true;
      } else {
        *out_value = default_value;
        return false;
      }
    }

    template<typename T>
    static T DecodeParameterWithDefault(
      const char* key,
      const flutter::EncodableMap& params,
      const T& default_value
    ) {
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<T>(it->second)) {
        return std::get<T>(it->second);
      } else {
        return default_value;
      }
    }

    // Overload for enum types (e.g., ShapeType)
    template<typename T>
    static void DecodeEnumParameterWithDefault(
      const char* key,
      T* out_value,
      const flutter::EncodableMap& params,
      const T& default_value,
      std::enable_if_t<std::is_enum<T>::value>* = nullptr
    ) {
      using UnderlyingType = typename std::underlying_type<T>::type;
      auto it = params.find(flutter::EncodableValue(key));
      if (it != params.end() && std::holds_alternative<UnderlyingType>(it->second)) {
        *out_value = static_cast<T>(std::get<UnderlyingType>(it->second));
      } else {
        *out_value = default_value;
      }
    }

    static void DecodeParameterWithDefault(
      const char* key,
      std::optional<std::unique_ptr<Material>>& out_value,
      const flutter::EncodableMap& params
    );

    static bool DecodeParameterWithDefault(
      const char* key,
      filament::math::float3* out_value,
      const flutter::EncodableMap& params,
      const filament::math::float3& default_value
    );

    static void DecodeParameterWithDefault(
      const char* key,
      filament::math::quatf* out_value,
      const flutter::EncodableMap& params,
      const filament::math::quatf& default_value
    );

    static void DecodeParameterWithDefault(
      const char* key,
      double* out_value,
      const flutter::EncodableMap& params,
      const double& default_value
    );

    static void DecodeParameterWithDefault(
      const char* key,
      std::string* out_value,
      const flutter::EncodableMap& params,
      const std::string& default_value
    );

    // Note std::get only supports double, this will take your double
    // and cast to float; this of course can truncate your value
    static void DecodeParameterWithDefault(
      const char* key,
      float* out_value,
      const flutter::EncodableMap& params,
      const float& default_value
    );

    // Takes either int32 or int64 and casts to int64
    static void DecodeParameterWithDefaultInt64(
      const char* key,
      int64_t* out_value,
      const flutter::EncodableMap& params,
      const int64_t& default_value
    );
};
}  // namespace plugin_filament_view
