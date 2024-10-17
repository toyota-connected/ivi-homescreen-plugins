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
#include "deserialize.h"

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
::filament::math::float3 Deserialize::Format3(
    const flutter::EncodableMap& map) {
  double x = 0, y = 0, z = 0.0f;

  for (const auto& [fst, snd] : map) {
    if (snd.IsNull() || !std::holds_alternative<double>(snd))
      continue;

    auto currValue = std::get<double>(snd);

    if (auto key = std::get<std::string>(fst); key == "x") {
      x = currValue;
    } else if (key == "y") {
      y = currValue;
    } else if (key == "z") {
      z = currValue;
    }
  }
  return {static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

////////////////////////////////////////////////////////////////////////////
::filament::math::quatf Deserialize::Format4(const flutter::EncodableMap& map) {
  double x = 0, y = 0, z = 0, w = 0.0f;
  w = 1.0f;

  for (const auto& [fst, snd] : map) {
    if (snd.IsNull())
      continue;

    if (auto key = std::get<std::string>(fst);
        key == "x" && std::holds_alternative<double>(snd)) {
      x = std::get<double>(snd);
    } else if (key == "y" && std::holds_alternative<double>(snd)) {
      y = std::get<double>(snd);
    } else if (key == "z" && std::holds_alternative<double>(snd)) {
      z = std::get<double>(snd);
    } else if (key == "w" && std::holds_alternative<double>(snd)) {
      w = std::get<double>(snd);
    }
  }
  return {static_cast<float>(w), static_cast<float>(x), static_cast<float>(y),
          static_cast<float>(z)};
}

////////////////////////////////////////////////////////////////////////////
void Deserialize::DecodeParameterWithDefault(
    const char* key,
    std::optional<std::unique_ptr<MaterialDefinitions>>& out_value,
    const flutter::EncodableMap& params,
    const std::string& flutter_assets_path) {
  if (auto it = params.find(flutter::EncodableValue(key));
      it != params.end() &&
      std::holds_alternative<flutter::EncodableMap>(it->second)) {
    out_value = std::make_unique<MaterialDefinitions>(
        flutter_assets_path, std::get<flutter::EncodableMap>(it->second));
  } else {
    out_value.reset();  // or set it to std::nullopt if desired
  }
}

////////////////////////////////////////////////////////////////////////////
void Deserialize::DecodeParameterWithDefault(
    const char* key,
    filament::math::float3* out_value,
    const flutter::EncodableMap& params,
    const filament::math::float3& default_value) {
  if (auto it = params.find(flutter::EncodableValue(key));
      it != params.end() &&
      std::holds_alternative<flutter::EncodableMap>(it->second)) {
    *out_value =
        Deserialize::Format3(std::get<flutter::EncodableMap>(it->second));
  } else {
    *out_value = default_value;
  }
}

////////////////////////////////////////////////////////////////////////////
void Deserialize::DecodeParameterWithDefault(
    const char* key,
    filament::math::quatf* out_value,
    const flutter::EncodableMap& params,
    const filament::math::quatf& default_value) {
  if (auto it = params.find(flutter::EncodableValue(key));
      it != params.end() &&
      std::holds_alternative<flutter::EncodableMap>(it->second)) {
    *out_value =
        Deserialize::Format4(std::get<flutter::EncodableMap>(it->second));
  } else {
    *out_value = default_value;
  }
}

////////////////////////////////////////////////////////////////////////////
void Deserialize::DecodeParameterWithDefault(
    const char* key,
    double* out_value,
    const flutter::EncodableMap& params,
    const double& default_value) {
  if (auto it = params.find(flutter::EncodableValue(key));
      it != params.end() && std::holds_alternative<double>(it->second)) {
    *out_value = std::get<double>(it->second);
  } else {
    *out_value = default_value;
  }
}

////////////////////////////////////////////////////////////////////////////
void Deserialize::DecodeParameterWithDefaultInt64(
    const char* key,
    int64_t* out_value,
    const flutter::EncodableMap& params,
    const int64_t& default_value) {
  if (auto it = params.find(flutter::EncodableValue(key));
      it != params.end() && std::holds_alternative<int64_t>(it->second)) {
    *out_value = std::get<int64_t>(it->second);
  } else {
    *out_value = default_value;
  }
}

}  // namespace plugin_filament_view
