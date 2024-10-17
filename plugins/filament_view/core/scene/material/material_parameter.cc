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

#include "material_parameter.h"

#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include <plugins/common/common.h>

namespace plugin_filament_view {

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialParameter(std::string name,
                                     MaterialType type,
                                     MaterialTextureValue value)
    : name_(std::move(name)), type_(type), textureValue_(std::move(value)) {}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialParameter(std::string name,
                                     MaterialType type,
                                     MaterialFloatValue value)
    : name_(std::move(name)), type_(type), fValue_(value) {}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialParameter(std::string name,
                                     MaterialType type,
                                     MaterialColorValue value)
    : name_(std::move(name)), type_(type), colorValue_(value) {}

////////////////////////////////////////////////////////////////////////////
std::unique_ptr<MaterialParameter> MaterialParameter::Deserialize(
    const std::string& /* flutter_assets_path */,
    const flutter::EncodableMap& params) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);

  std::optional<std::string> name;
  std::optional<MaterialType> type;
  std::optional<MaterialFloatValue> fValue;
  std::optional<MaterialColorValue> colorValue;
  std::optional<flutter::EncodableMap> encodMapValue;

  for (const auto& [fst, snd] : params) {
    auto key = std::get<std::string>(fst);
    if (snd.IsNull()) {
      SPDLOG_DEBUG("MaterialParameter Param Second mapping is null {} {} {}",
                   key, __FILE__, __FUNCTION__);
      continue;
    }

    if (key == "name" && std::holds_alternative<std::string>(snd)) {
      name = std::get<std::string>(snd);
    } else if (key == "type" && std::holds_alternative<std::string>(snd)) {
      type = getTypeForText(std::get<std::string>(snd));
    } else if (key == "value" && type == MaterialType::FLOAT) {
      fValue = std::get<double>(snd);
    } else if (key == "value" && type == MaterialType::COLOR) {
      // color comes across a a radix string #FFFFFFFF
      colorValue = HexToColorFloat4(std::get<std::string>(snd));
    } else if (key == "value" && type == MaterialType::TEXTURE) {
      encodMapValue = std::get<flutter::EncodableMap>(snd);
    } else if (!snd.IsNull()) {
      spdlog::debug("[MaterialParameter] Unhandled Parameter {} ", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }

  if (!type.has_value()) {
    spdlog::error(
        "[MaterialParameter::Deserialize] Unhandled Parameter - no type in arg "
        "list");
    return {};
  }

  switch (type.value()) {
    case MaterialType::TEXTURE:
      return std::make_unique<MaterialParameter>(
          name.has_value() ? name.value() : "", type.value(),
          TextureDefinitions::Deserialize(encodMapValue.value()));

    case MaterialType::FLOAT:
      return std::make_unique<MaterialParameter>(
          name.has_value() ? name.value() : "", type.value(), fValue.value());

    case MaterialType::COLOR:
      return std::make_unique<MaterialParameter>(
          name.has_value() ? name.value() : "", type.value(),
          colorValue.value());

    default:
      spdlog::error("[MaterialParameter::Deserialize] Unhandled Parameter {}",
                    getTextForType(type.value()));
      return {};
  }
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::~MaterialParameter() = default;

////////////////////////////////////////////////////////////////////////////
void MaterialParameter::DebugPrint(const char* tag) {
  spdlog::debug("++++++++ (MaterialParameter) ++++++++");
  spdlog::debug("tag {} name {} type {} ", tag, name_, getTextForType(type_));
  if (type_ == MaterialType::TEXTURE) {
    if (textureValue_.has_value()) {
      const auto texture =
          std::get<std::unique_ptr<TextureDefinitions>>(textureValue_.value())
              .get();
      if (texture) {
        texture->DebugPrint("texture");
      } else {
        spdlog::debug("[MaterialParameter] Texture Empty");
      }
    }
  }
  spdlog::debug("-------- (MaterialParameter) --------");
}

////////////////////////////////////////////////////////////////////////////
const char* MaterialParameter::getTextForType(MaterialType type) {
  return (const char*[]){
      kColor, kBool,      kBoolVector, kFloat, kFloatVector,
      kInt,   kIntVector, kMat3,       kMat4,  kTexture,
  }[static_cast<int>(type)];
}

////////////////////////////////////////////////////////////////////////////
MaterialParameter::MaterialType MaterialParameter::getTypeForText(
    const std::string& type) {
  // TODO Change to map for faster lookup
  if (type == kColor) {
    return MaterialType::COLOR;
  }
  if (type == kBool) {
    return MaterialType::BOOL;
  }
  if (type == kBoolVector) {
    return MaterialType::BOOL_VECTOR;
  }
  if (type == kFloat) {
    return MaterialType::FLOAT;
  }
  if (type == kFloatVector) {
    return MaterialType::FLOAT_VECTOR;
  }
  if (type == kInt) {
    return MaterialType::INT;
  }
  if (type == kIntVector) {
    return MaterialType::INT_VECTOR;
  }
  if (type == kMat3) {
    return MaterialType::MAT3;
  }
  if (type == kMat4) {
    return MaterialType::MAT4;
  }
  if (type == kTexture) {
    return MaterialType::TEXTURE;
  }
  return MaterialType::INT;
}

////////////////////////////////////////////////////////////////////////////
MaterialColorValue MaterialParameter::HexToColorFloat4(const std::string& hex) {
  // Ensure the string starts with '#' and is the correct length
  if (hex[0] != '#' || hex.length() != 9) {
    throw std::invalid_argument("Invalid hex color format");
  }

  // Comes across from our dart as ARGB

  // Extract the hex values for each channel
  unsigned int r, g, b, a;
  std::stringstream ss;
  ss << std::hex << hex.substr(1, 2);
  ss >> a;
  ss.clear();
  ss << std::hex << hex.substr(3, 2);
  ss >> r;
  ss.clear();
  ss << std::hex << hex.substr(5, 2);
  ss >> g;
  ss.clear();
  ss << std::hex << hex.substr(7, 2);
  ss >> b;

  // Convert to float in the range [0, 1]
  MaterialColorValue color;
  color.r = static_cast<float>(r) / 255.0f;
  color.g = static_cast<float>(g) / 255.0f;
  color.b = static_cast<float>(b) / 255.0f;
  color.a = static_cast<float>(a) / 255.0f;

  return color;
}

}  // namespace plugin_filament_view