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

#include "material_definitions.h"

#include <filament/Material.h>
#include <filament/TextureSampler.h>
#include <plugins/common/common.h>
#include <filesystem>

namespace plugin_filament_view {

using MinFilter = filament::TextureSampler::MinFilter;
using MagFilter = filament::TextureSampler::MagFilter;

///////////////////////////////////////////////////////////////////////////////////////////////////
MaterialDefinitions::MaterialDefinitions(const std::string& flutter_assets_path,
                                         const flutter::EncodableMap& params)
    : flutterAssetsPath_(flutter_assets_path) {
  SPDLOG_TRACE("++{}::{}", __FILE__, __FUNCTION__);
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
    } else if (key == "parameters" &&
               std::holds_alternative<flutter::EncodableList>(snd)) {
      auto list = std::get<flutter::EncodableList>(snd);
      for (const auto& it_ : list) {
        auto parameter = MaterialParameter::Deserialize(
            flutter_assets_path, std::get<flutter::EncodableMap>(it_));
        parameters_.insert(
            std::pair(parameter->szGetParameterName(), std::move(parameter)));
      }
    } else if (!snd.IsNull()) {
      spdlog::debug("[Material] Unhandled Parameter {}", key.c_str());
      plugin_common::Encodable::PrintFlutterEncodableValue(key.c_str(), snd);
    }
  }
  SPDLOG_TRACE("--{}::{}", __FILE__, __FUNCTION__);
}

///////////////////////////////////////////////////////////////////////////////////////////////////
MaterialDefinitions::~MaterialDefinitions() {
  for (auto& [fst, snd] : parameters_) {
    snd.reset();
  }
  parameters_.clear();
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MaterialDefinitions::DebugPrint(const char* tag) {
  spdlog::debug("++++++++ (MaterialDefinitions) ++++++++");
  spdlog::debug("{}", tag);
  if (!assetPath_.empty()) {
    spdlog::debug("assetPath: [{}]", assetPath_);
    const std::filesystem::path asset_folder(flutterAssetsPath_);
    spdlog::debug("asset_path {} valid",
                  exists(asset_folder / assetPath_) ? "is" : "is not");
  }
  if (!url_.empty()) {
    spdlog::debug("url: [{}]", url_);
  }
  spdlog::debug("ParamCount: [{}]", parameters_.size());

  for (const auto& [fst, snd] : parameters_) {
    if (snd != nullptr)
      snd->DebugPrint("\tparameter");
  }

  spdlog::debug("-------- (MaterialDefinitions) --------");
}

///////////////////////////////////////////////////////////////////////////////////////////////////
std::string MaterialDefinitions::szGetMaterialDefinitionLookupName() const {
  if (!assetPath_.empty()) {
    return assetPath_;
  }
  if (!url_.empty()) {
    return url_;
  }
  return "Unknown";
}

///////////////////////////////////////////////////////////////////////////////////////////////////
std::vector<MaterialParameter*>
MaterialDefinitions::vecGetTextureMaterialParameters() const {
  std::vector<MaterialParameter*> returnVector;

  for (const auto& [fst, snd] : parameters_) {
    // Check if the type is TEXTURE
    if (snd->type_ == MaterialParameter::MaterialType::TEXTURE) {
      returnVector.push_back(snd.get());
    }
  }

  return returnVector;
}

///////////////////////////////////////////////////////////////////////////////////////////////////
void MaterialDefinitions::vSetMaterialInstancePropertiesFromMyPropertyMap(
    const filament::Material* materialResult,
    filament::MaterialInstance* materialInstance,
    const TextureMap& loadedTextures) const {
  const auto count = materialResult->getParameterCount();
  std::vector<filament::Material::ParameterInfo> parameters(count);

  const auto actual = materialResult->getParameters(parameters.data(), count);
  assert(count == actual && actual == parameters.size());

  for (const auto& param : parameters) {
    if (param.name) {
      SPDLOG_TRACE("[Material] name: {}, type: {}", param.name,
                   static_cast<int>(param.type));

      const auto& iter = parameters_.find(param.name);
      if (iter == parameters_.end() || iter->second == nullptr) {
        // This can get pretty spammy, but good if needing to debug further into
        // parameter values.
        SPDLOG_INFO("No default parameter value available for {}::{} {}",
                    __FILE__, __FUNCTION__, param.name);
        continue;
      }
      SPDLOG_TRACE("Setting material param {}", param.name);

      switch (iter->second->type_) {
        case MaterialParameter::MaterialType::COLOR: {
          materialInstance->setParameter(param.name, filament::RgbaType::LINEAR,
                                         iter->second->colorValue_.value());
        } break;

        case MaterialParameter::MaterialType::FLOAT: {
          materialInstance->setParameter(param.name,
                                         iter->second->fValue_.value());
        } break;

        case MaterialParameter::MaterialType::TEXTURE: {
          // make sure we have the texture:
          auto foundResource =
              loadedTextures.find(iter->second->getTextureValueAssetPath());

          if (foundResource == loadedTextures.end()) {
            // log and continue
            spdlog::warn(
                "Got to a case where a texture was not loaded before trying to "
                "apply to a material.");
            continue;
          }

          // sampler will be on 'our' deserialized
          // texturedefinitions->texture_sampler
          const auto textureSampler = iter->second->getTextureSampler();

          filament::TextureSampler sampler(MinFilter::LINEAR,
                                           MagFilter::LINEAR);

          if (textureSampler != nullptr) {
            // SPDLOG_INFO("Overloading filtering options with set param
            // values");
            sampler.setMinFilter(textureSampler->getMinFilter());
            sampler.setMagFilter(textureSampler->getMagFilter());
            sampler.setAnisotropy(
                static_cast<float>(textureSampler->getAnisotropy()));

            // Currently leaving this commented out, but this is for 3d
            // textures, which are not currently expected to be loaded
            // as time of writing.
            // sampler.setWrapModeR(textureSampler->getWrapModeR());

            sampler.setWrapModeS(textureSampler->getWrapModeS());
            sampler.setWrapModeT(textureSampler->getWrapModeT());
          }

          if (!foundResource->second.getData().has_value()) {
            spdlog::warn(
                "Got to a case where a texture resource data was not loaded "
                "before trying to "
                "apply to a material.");
            continue;
          }

          const auto texture = foundResource->second.getData().value();
          materialInstance->setParameter(param.name, texture, sampler);
        } break;

        default: {
          SPDLOG_WARN("Type template not setup yet, see {} {}", __FILE__,
                      __FUNCTION__);
        } break;
      }
    }
  }
}

}  // namespace plugin_filament_view