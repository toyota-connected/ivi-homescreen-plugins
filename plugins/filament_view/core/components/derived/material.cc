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

#include "material.h"

#include <core/include/literals.h>
#include <core/systems/ecs.h>
#include <filament/Material.h>
#include <filesystem>
#include <plugins/common/common.h>

namespace plugin_filament_view {

///////////////////////////////////////////////////////////////////////////////////////////////////
Material::Material(const flutter::EncodableMap& params)
  : Component(std::string(__FUNCTION__)) {
  SPDLOG_TRACE("++{}", __FUNCTION__);
  const auto flutterAssetPath = ECSManager::GetInstance()->getConfigValue<std::string>(kAssetPath);

  // TODO: rewrite this without the for
  for (const auto& [fst, snd] : params) {
    auto key = std::get<std::string>(fst);
    SPDLOG_TRACE("Material Param {}", key);

    if (snd.IsNull() && key != "url") {
      SPDLOG_WARN("Material Param Second mapping is null {}", key);
      continue;
    }

    if (snd.IsNull() && key == "url") {
      SPDLOG_TRACE("Material Param URL mapping is null {}", key);
      continue;
    }

    if (key == "assetPath" && std::holds_alternative<std::string>(snd)) {
      assetPath_ = std::get<std::string>(snd);
    } else if (key == "url" && std::holds_alternative<std::string>(snd)) {
      url_ = std::get<std::string>(snd);
    } else if (key == "parameters" && std::holds_alternative<flutter::EncodableList>(snd)) {
      auto list = std::get<flutter::EncodableList>(snd);
      for (const auto& it_ : list) {
        auto parameter = MaterialParameter::Deserialize(
          flutterAssetPath, std::get<flutter::EncodableMap>(it_)
        );
        _tmpParams.emplace_back(std::move(parameter));
      }
    } else if (!snd.IsNull()) {
      spdlog::debug("[Material] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
  SPDLOG_TRACE("--{}", __FUNCTION__);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
Material::~Material() {
  for (auto& param : _tmpParams) {
    param.reset();
  }
  _tmpParams.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void Material::debugPrint(const std::string& tabPrefix) const {
  spdlog::debug(tabPrefix + "++++++++ (Material) ++++++++");
  if (!assetPath_.empty()) {
    spdlog::debug(tabPrefix + "assetPath: [{}]", assetPath_);

    const auto flutterAssetPath = ECSManager::GetInstance()->getConfigValue<std::string>(kAssetPath
    );

    const std::filesystem::path asset_folder(flutterAssetPath);
    spdlog::debug(
      tabPrefix + "asset_path {} valid", exists(asset_folder / assetPath_) ? "is" : "is not"
    );
  }
  if (!url_.empty()) {
    spdlog::debug(tabPrefix + "url: [{}]", url_);
  }
  spdlog::debug(tabPrefix + "ParamCount: [{}]", _tmpParams.size());

  for (const auto& param : _tmpParams) {
    if (param != nullptr)
      // param->debugPrint(std::string(tabPrefix + "parameter").c_str());
      spdlog::debug(
        tabPrefix + "parameter: {} type: {}", param->getName(), static_cast<int>(param->type_)
      );
  }

  spdlog::debug("-------- (Material) --------");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
const std::string* Material::getLookupName() const {
  if (!assetPath_.empty()) {
    return &assetPath_;
  } else if (!url_.empty()) {
    return &url_;
  } else {
    return nullptr;
  }
}

///////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<MaterialParameter*> Material::getTextureMaterialParameters() const {
  std::vector<MaterialParameter*> returnVector;

  for (const auto& param : _tmpParams) {
    // Check if the type is TEXTURE
    if (param->type_ == MaterialParameter::MaterialType::TEXTURE) {
      returnVector.push_back(param.get());
    }
  }

  return returnVector;
}

void Material::setInstanceProperty(std::shared_ptr<MaterialParameter> param) {
  _tmpParams.push_back(std::move(param));
}

}  // namespace plugin_filament_view
