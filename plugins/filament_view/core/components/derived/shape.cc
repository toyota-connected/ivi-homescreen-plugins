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

#include "shape.h"

#include <core/include/literals.h>
#include <core/utils/asserts.h>
#include <core/utils/deserialize.h>

namespace plugin_filament_view {

Shape::Shape(const flutter::EncodableMap& params)
  : Component(std::string(__FUNCTION__)),
    type(ShapeType::Unset),
    isWireframe(false),
    _vertexBuffer(nullptr),
    _indexBuffer(nullptr),
    _params() {

  // Find the "type" key in the params
  if (const auto it = params.find(flutter::EncodableValue(kType));
      it != params.end() && std::holds_alternative<int32_t>(it->second)) {
    // Check if the value is within the valid range of the ShapeType enum
    if (int32_t typeValue = std::get<int32_t>(it->second);
        typeValue > static_cast<int32_t>(ShapeType::Unset)
        && typeValue < static_cast<int32_t>(ShapeType::Max)) {
      type = static_cast<ShapeType>(typeValue);
    } else {
      spdlog::error("Invalid shape type value: {}", typeValue);
      throw std::invalid_argument("Invalid shape type value");
    }
  } else {
    spdlog::error("'type' not found or is of incorrect type");
    throw std::invalid_argument("'type' not found or is of incorrect type");
  }

  // isWireframe
  Deserialize::DecodeParameterWithDefault(kIsWireframe, &isWireframe, params, false);

  // _params
  if (const auto it = params.find(flutter::EncodableValue(kParams));
      it != params.end() && std::holds_alternative<flutter::EncodableMap>(it->second)) {
    auto paramMap = std::get<flutter::EncodableMap>(it->second);
    for (const auto& [key, value] : paramMap) {
      if (std::holds_alternative<double>(value)) {
        _params[std::get<std::string>(key)] = static_cast<float>(std::get<double>(value));
      } else if (std::holds_alternative<int32_t>(value)) {
        _params[std::get<std::string>(key)] = static_cast<float>(std::get<int32_t>(value));
      } else if (std::holds_alternative<int64_t>(value)) {
        _params[std::get<std::string>(key)] = static_cast<float>(std::get<int64_t>(value));
      } else if (std::holds_alternative<float>(value)) {
        _params[std::get<std::string>(key)] = std::get<float>(value);
      } else {
        spdlog::warn("Parameter '{}' has unsupported type", std::get<std::string>(key));
      }
    }
  }
}

void Shape::debugPrint(const std::string& prefix) const {
  Component::debugPrint(prefix);
  spdlog::debug("{}  type: {}", prefix, shapeTypeToString(type));
  spdlog::debug("{}  isWireframe: {}", prefix, isWireframe);
}

}  // namespace plugin_filament_view
