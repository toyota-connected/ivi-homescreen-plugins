/*
 * Copyright 2020-2023 Toyota Connected North America
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

#include "material_definitions.h"

#include <filesystem>

#include "material_manager.h"
#include "plugins/common/common.h"

namespace plugin_filament_view {

///////////////////////////////////////////////////////////////////////////////////////////////////
MaterialDefinitions::MaterialDefinitions(const std::string& flutter_assets_path,
                                         const flutter::EncodableMap& params)
    : flutterAssetsPath_(flutter_assets_path) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);
  for (auto& it : params) {
    auto key = std::get<std::string>(it.first);
    SPDLOG_TRACE("Material Param {}", key);

    if (it.second.IsNull() && key != "url") {
      SPDLOG_WARN("Material Param Second mapping is null {}", key);
      continue;
    } else if (it.second.IsNull() && key == "url") {
      SPDLOG_TRACE("Material Param URL mapping is null {}", key);
      continue;
    }

    if (key == "assetPath" && std::holds_alternative<std::string>(it.second)) {
      assetPath_ = std::get<std::string>(it.second);
    } else if (key == "url" && std::holds_alternative<std::string>(it.second)) {
      url_ = std::get<std::string>(it.second);
    } else if (key == "parameters" &&
               std::holds_alternative<flutter::EncodableList>(it.second)) {
      auto list = std::get<flutter::EncodableList>(it.second);
      for (const auto& it_ : list) {
        auto parameter = MaterialParameter::Deserialize(
            flutter_assets_path, std::get<flutter::EncodableMap>(it_));
        parameters_.insert(
            std::pair(parameter->szGetParameterName(), std::move(parameter)));
      }
    } else if (!it.second.IsNull()) {
      spdlog::debug("[Material] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(),
                                                           it.second);
    }
  }
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
MaterialDefinitions::~MaterialDefinitions() {
  for (auto& item : parameters_) {
    item.second.reset();
  }
  parameters_.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MaterialDefinitions::DebugPrint(const char* tag) {
  spdlog::debug("++++++++");
  spdlog::debug("{} (MaterialDefinitions)", tag);
  if (!assetPath_.empty()) {
    spdlog::debug("\tassetPath: [{}]", assetPath_);
    std::filesystem::path asset_folder(flutterAssetsPath_);
    spdlog::debug(
        "\tasset_path {} valid",
        std::filesystem::exists(asset_folder / assetPath_) ? "is" : "is not");
  }
  if (!url_.empty()) {
    spdlog::debug("\turl: [{}]", url_);
  }
  spdlog::debug("\tParamCount: [{}]", parameters_.size());

  for (const auto& param : parameters_) {
    if (param.second != nullptr)
      param.second->Print("\tparameter");
  }
  spdlog::debug("++++++++");
}

std::string MaterialDefinitions::szGetMaterialDefinitionLookupName()
    const {
  if (!assetPath_.empty()) {
    return assetPath_;
  }
  if (!url_.empty()) {
    return url_;
  }
  return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MaterialDefinitions::vSetMaterialInstancePropertiesFromMyPropertyMap(
    const ::filament::Material* materialResult,
    filament::MaterialInstance* materialInstance) const {
  auto count = materialResult->getParameterCount();
  std::vector<::filament::Material::ParameterInfo> parameters(count);

  auto actual = materialResult->getParameters(parameters.data(), count);
  assert(count == actual && actual == parameters.size());

  for (const auto& param : parameters) {
    if (param.name) {
      SPDLOG_TRACE("[Material] name: {}, type: {}", param.name,
                   static_cast<int>(param.type));

      const auto& iter = parameters_.find(param.name);
      if (iter != parameters_.end() && iter->second != nullptr) {
        SPDLOG_TRACE("Setting material param {}", param.name);

        // Should probably check to make sure the two types match as well
        // TODO
        auto& parameterType = iter->second->type_;

        // todo make sure has values? might be overkill.
        switch (parameterType) {
          case MaterialParameter::MaterialType::COLOR: {
            materialInstance->setParameter(param.name,
                                           filament::RgbaType::LINEAR,
                                           iter->second->colorValue_.value());
          } break;

          case MaterialParameter::MaterialType::FLOAT: {
            materialInstance->setParameter(param.name,
                                           iter->second->fValue_.value());
          } break;

          case MaterialParameter::MaterialType::TEXTURE:
            // materialInstance->setParameter(param.name,
            // iter->second->textureValue_.value());
          default: {
            SPDLOG_WARN("Type template not setup yet, see {} {}", __FILE__,
                        __FUNCTION__);
          } break;
        }
      } else {
        // This can get pretty spammy, but good if needing to debug further into
        // parameter values. SPDLOG_INFO("No default parameter value available
        // for {}::{} {}", __FILE__, __FUNCTION__, param.name);
      }
    }
  }
}

}  // namespace plugin_filament_view